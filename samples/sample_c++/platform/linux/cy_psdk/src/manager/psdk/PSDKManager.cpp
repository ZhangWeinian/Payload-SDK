// cy_psdk/manager/psdk/PSDKManager.cpp

#include "manager/psdk/PSDKManager.h"

#include "application.hpp"
#include <dji_camera_manager.h>
#include <dji_fc_subscription.h>
#include <dji_flight_controller.h>
#include <dji_gimbal_manager.h>
#include <dji_hms_manager.h>
#include <dji_liveview.h>
#include <dji_logger.h>
#include <dji_platform.h>

#include "config/ConfigManager.h"
#include "manager/plane_state/PlaneStateStore.h"
#include "manager/psdk/PSDKAdapter.h"
#include "utils/DjiErrorUtils.h"
#include "utils/log_util/Logger.h"

#include <fmt/format.h>

#include <string_view>
#include <chrono>
#include <string>

namespace plane::manager
{
    namespace
    {
        // 将 PSDK 日志重定向到 spdlog
        ::T_DjiReturnCode psdkLogRedirectCallback(const ::std::uint8_t* data, ::std::uint16_t dataLen)
        {
            ::std::string message(reinterpret_cast<const char*>(data), dataLen);
            while (!message.empty() && (message.back() == '\n' || message.back() == '\r'))
            {
                message.pop_back();
            }

            plane::utils::Logger::getInstance().PSDKLogRedirection(message);
            return ::DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
        }
    } // namespace

    PSDKManager& PSDKManager::getInstance(void) noexcept
    {
        static PSDKManager instance {};
        return instance;
    }

    bool PSDKManager::isCameraInitialized(void) const noexcept
    {
        return this->camera_initialized_;
    }

    bool PSDKManager::isHmsInitialized(void) const noexcept
    {
        return this->hms_initialized_;
    }

    PSDKManager::LicenseLevel PSDKManager::licenseLevel(void) const noexcept
    {
        return this->license_level_;
    }

    bool PSDKManager::isAdvancedLicenseAvailable(void) const noexcept
    {
        return this->license_level_ == LicenseLevel::ADVANCED;
    }

    bool PSDKManager::isLiveviewInitialized(void) const noexcept
    {
        return this->liveview_initialized_;
    }

    bool PSDKManager::isGimbalManagerInitialized(void) const noexcept
    {
        return this->gimbal_manager_initialized_;
    }

    void PSDKManager::redirectPsdkLogs(void) noexcept
    {
        static ::std::atomic<bool> redirected { false };
        if (redirected.load())
        {
            return;
        }

        const auto& config { plane::config::ConfigManager::getInstance() };

        if (::T_DjiLoggerConsole console = { .func         = psdkLogRedirectCallback,
                                             .consoleLevel = static_cast<uint8_t>(config.getPsdkLogLevel()),
                                             // 必须为 false: PSDK 开启颜色后会在字段"中间"插入 ANSI 转义码,
                                             // 而 Logger::PSDKLogRedirection() 的解析正则不接受字段间夹颜色码
                                             // (实测失配) ⇒ 会退化到兼底分支原样输出, 源位置为空显示 "[:]",
                                             // 日志变成难读的固定列宽原始行。反正颜色到不了终端(我们用自己的 sink),
                                             // 关掉即可让全部行都能被正常解析美化。
                                             .isSupportColor = false };
            ::DjiLogger_AddConsole(&console) != ::DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
        {
            // 失败不置位: 允许平台就绪后再次尝试
            LOG_WARN("重定向 PSDK 日志失败。可能会看到重复或格式不一的日志");
        }
        else
        {
            redirected.store(true);
            LOG_INFO("PSDK 日志已成功重定向到 spdlog");
        }
    }

    PSDKManager::PSDKManager(void) noexcept = default;

    PSDKManager::~PSDKManager(void) noexcept
    {
        try
        {
            LOG_DEBUG("PSDKManager 正在析构");
            this->stop();
        }
        catch (const ::std::exception& e)
        {
            LOG_ERROR("PSDKManager 析构异常: {}", e.what());
        }
        catch (...)
        {
            LOG_ERROR("PSDKManager 析构发生未知异常: <non-std exception>");
        }
    }

    bool PSDKManager::start(void)
    {
        // 确保幂等性
        if (bool expected { false }; !this->running_.compare_exchange_strong(expected, true))
        {
            LOG_WARN("PSDKManager::start() 被重复调用，已忽略");
            return true;
        }
        else
        {
            LOG_DEBUG("PSDKManager::start() 正在执行");
        }

        // 获取配置管理器实例
        const auto& config { plane::config::ConfigManager::getInstance() };

        try
        {
            LOG_INFO("--- PSDK 底层服务初始化开始 ---");

            // 重置各模块初始化标志, 防止上次未完整清理的残留状态影响本次启动
            this->hms_initialized_             = false;
            this->camera_initialized_          = false;
            this->liveview_initialized_        = false;
            this->gimbal_manager_initialized_  = false;
            this->fc_initialized_              = false;
            this->fc_subscription_initialized_ = false;
            this->adapter_subscribed_          = false;
            this->license_level_               = LicenseLevel::UNKNOWN;

            // 重定向 PSDK 日志到 spdlog (幂等; 入口通常已提前调用, 此处兜底)
            this->redirectPsdkLogs();

            LOG_INFO("DJI PSDK Application 初始化完成");

            // 初始化 HMS 模块
            if (::T_DjiReturnCode returnCode {
                    ::DjiHmsManager_Init(),
                };
                returnCode != ::DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
            {
                LOG_WARN("HMS 模块初始化失败, 错误: {}", plane::utils::convertDjiError(returnCode));
            }
            else
            {
                this->hms_initialized_ = true;
                LOG_INFO("HMS 模块初始化完成");
            }

            // 初始化相机模块 (高级功能: 相机管理)
            // 同时作为"许可等级"探针: 成功 → 高级许可; NONSUPPORT(Invalid license) → 基础许可
            const ::T_DjiReturnCode cameraReturnCode { ::DjiCameraManager_Init() };
            if (cameraReturnCode != ::DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
            {
                LOG_WARN("相机管理初始化失败 (错误码 {:#010X}): {}", cameraReturnCode, plane::utils::convertDjiError(cameraReturnCode));
            }
            else
            {
                this->camera_initialized_ = true;
                LOG_INFO("相机管理初始化完成");
            }

            // ---- 许可等级判定 + 高级模块初始化 (相机管理/取流/云台管理/运动规划 均属高级功能) ----
            if (this->camera_initialized_)
            {
                this->license_level_ = LicenseLevel::ADVANCED;
            }
            else if (cameraReturnCode == ::DJI_ERROR_SYSTEM_MODULE_CODE_NONSUPPORT)
            {
                this->license_level_ = LicenseLevel::BASIC;
                LOG_WARN(
                    "当前为基础许可: 相机管理/取流(liveview)/云台管理/运动规划 等高级功能不可用 "
                    "(如已购买高级许可, 请确认开发者 App 信息与飞机已绑定)"
                );
            }
            else
            {
                this->license_level_ = LicenseLevel::UNKNOWN;
                LOG_WARN("许可等级判定失败 (相机管理返回非许可类错误), 本次不初始化任何高级模块");
            }

            if (this->license_level_ == LicenseLevel::ADVANCED)
            {
                LOG_INFO("检测到高级许可, 启用高级功能模块");

                // 取流模块: 获取飞机相机 H.264 码流 (高级)
                if (::T_DjiReturnCode returnCode { ::DjiLiveview_Init() }; returnCode != ::DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
                {
                    LOG_WARN("取流(liveview)模块初始化失败, 错误: {}", plane::utils::convertDjiError(returnCode));
                }
                else
                {
                    this->liveview_initialized_ = true;
                    LOG_INFO("取流(liveview)模块初始化完成");
                }

                // 云台管理模块: 控制飞机云台 (高级; 基础档无对应能力)
                if (::T_DjiReturnCode returnCode { ::DjiGimbalManager_Init() }; returnCode != ::DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
                {
                    LOG_WARN("云台管理模块初始化失败, 错误: {}", plane::utils::convertDjiError(returnCode));
                }
                else
                {
                    this->gimbal_manager_initialized_ = true;
                    LOG_INFO("云台管理模块初始化完成");
                }
            }

            // 初始化飞控模块 (必须先初始化再调用任何 DjiFlightController_* API, 否则模块未就绪会崩溃)
            // ridInfo: RID 合规要求上报"真实起降点"; 数据源为 state_ 的独立字段 (不从 config.yml 读取;
            // 默认取模拟器默认坐标, 部署后由上层按实际位置更新)。
            // PSDK 契约: 初始化时一次性传入, 之后无任何更新接口
            // 字段级一致读取: 3 个字段同一把锁内取齐 (只复制 24 字节, 不整份快照)
            const auto [takeoff_lat_deg, takeoff_lon_deg, takeoff_alt_m] { plane::domain::PlaneStateStore::getInstance().read(
                &plane::domain::PlaneStateDataClass::rid_takeoff_latitude_deg,
                &plane::domain::PlaneStateDataClass::rid_takeoff_longitude_deg,
                &plane::domain::PlaneStateDataClass::rid_takeoff_altitude_m
            ) };
            ::T_DjiFlightControllerRidInfo ridInfo {};
            ridInfo.latitude  = takeoff_lat_deg * (1.0 / RAD_TO_DEG); // PSDK 要求弧度
            ridInfo.longitude = takeoff_lon_deg * (1.0 / RAD_TO_DEG);
            ridInfo.altitude  = static_cast<::std::uint16_t>(takeoff_alt_m);
            LOG_INFO("RID 起降点 (state_): 纬度={}, 经度={}, 海拔={}m", takeoff_lat_deg, takeoff_lon_deg, takeoff_alt_m);
            if (::T_DjiReturnCode returnCode { ::DjiFlightController_Init(ridInfo) }; returnCode != ::DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
            {
                LOG_ERROR("飞控模块初始化失败, 错误: {}", plane::utils::convertDjiError(returnCode));
                return false;
            }
            this->fc_initialized_ = true;
            LOG_INFO("飞控模块初始化完成");

            // 初始化数据订阅模块 (官方要求: 订阅任何主题之前先初始化)
            if (::T_DjiReturnCode returnCode { ::DjiFcSubscription_Init() }; returnCode != ::DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
            {
                LOG_WARN("数据订阅模块初始化失败 (订阅可能受限), 错误: {}", plane::utils::convertDjiError(returnCode));
            }
            else
            {
                this->fc_subscription_initialized_ = true;
                LOG_INFO("数据订阅模块初始化完成");
            }

            // 启动 PSDK 适配器
            if (!plane::manager::PSDKAdapter::getInstance().subscribeTelemetryData())
            {
                LOG_ERROR("PSDK 适配器订阅遥测数据失败！");
                return false;
            }
            this->adapter_subscribed_ = true;
            LOG_INFO("PSDK 适配器订阅遥测数据完成");

            // 根据配置决定是否禁用遥控器检测
            if (config.isStandardProceduresEnabled() && config.isSkipRC())
            {
                if (::T_DjiReturnCode returnCode {
                        ::DjiFlightController_SetRCLostActionEnableStatus(::DJI_FLIGHT_CONTROLLER_DISABLE_RC_LOST_ACTION),
                    };
                    returnCode != ::DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
                {
                    LOG_WARN("禁用 RC Lost Action 失败，错误: {}, 错误码: {:#08X}", plane::utils::convertDjiError(returnCode), returnCode);
                }
                else
                {
                    LOG_INFO("已成功发送禁用 RC Lost Action 的指令");
                }
            }

            LOG_INFO("--- PSDK 底层服务初始化成功 ---");
            return true;
        }
        catch (const ::std::exception& e)
        {
            LOG_ERROR("PSDK 底层服务初始化异常: {}", e.what());
            this->stop();
            return false;
        }
        catch (...)
        {
            LOG_ERROR("PSDK 底层服务初始化发生未知异常: <non-std exception>");
            this->stop();
            return false;
        }
    }

    void PSDKManager::stop(void)
    {
        // 确保幂等性
        if (bool expected { true }; !this->running_.compare_exchange_strong(expected, false))
        {
            return;
        }

        LOG_INFO("--- PSDK 底层服务反初始化开始 ---");

        // 仅当遥测订阅已成功建立时才清理 PSDK 适配器 (对未初始化模块调用反注册会崩溃)
        if (this->adapter_subscribed_)
        {
            plane::manager::PSDKAdapter::getInstance().unsubscribeTelemetryData();
            this->adapter_subscribed_ = false;
            LOG_INFO("PSDK 适配器清理完成");
        }

        // 仅反初始化已成功初始化的模块 (对未就绪模块调用 SDK 接口可能崩溃)
        if (this->fc_subscription_initialized_)
        {
            if (::T_DjiReturnCode returnCode { ::DjiFcSubscription_DeInit() }; returnCode != ::DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
            {
                LOG_WARN("数据订阅模块反初始化失败, 错误: {}", plane::utils::convertDjiError(returnCode));
            }
            this->fc_subscription_initialized_ = false;
        }

        if (this->fc_initialized_)
        {
            if (::T_DjiReturnCode returnCode { ::DjiFlightController_DeInit() }; returnCode != ::DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
            {
                LOG_WARN("飞控模块反初始化失败, 错误: {}", plane::utils::convertDjiError(returnCode));
            }
            this->fc_initialized_ = false;
        }

        if (this->hms_initialized_)
        {
            if (::T_DjiReturnCode returnCode { ::DjiHmsManager_DeInit() }; returnCode != ::DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
            {
                LOG_WARN("HMS 模块反初始化失败, 错误: {}", plane::utils::convertDjiError(returnCode));
            }
            this->hms_initialized_ = false;
        }

        if (this->gimbal_manager_initialized_)
        {
            if (::T_DjiReturnCode returnCode { ::DjiGimbalManager_Deinit() }; returnCode != ::DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
            {
                LOG_WARN("云台管理模块反初始化失败, 错误: {}", plane::utils::convertDjiError(returnCode));
            }
            this->gimbal_manager_initialized_ = false;
        }

        if (this->liveview_initialized_)
        {
            if (::T_DjiReturnCode returnCode { ::DjiLiveview_Deinit() }; returnCode != ::DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
            {
                LOG_WARN("取流(liveview)模块反初始化失败, 错误: {}", plane::utils::convertDjiError(returnCode));
            }
            this->liveview_initialized_ = false;
        }

        if (this->camera_initialized_)
        {
            if (::T_DjiReturnCode returnCode { ::DjiCameraManager_DeInit() }; returnCode != ::DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
            {
                LOG_WARN("相机模块反初始化失败, 错误: {}", plane::utils::convertDjiError(returnCode));
            }
            this->camera_initialized_ = false;
        }

        LOG_INFO("DJI PSDK Application 已反初始化");
        LOG_INFO("--- PSDK 底层服务反初始化完成 ---");
    }
} // namespace plane::manager

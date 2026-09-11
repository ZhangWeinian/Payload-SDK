// cy_psdk/manager/psdk/PSDKManager.cpp

#include "manager/psdk/PSDKManager.h"

#include "application.hpp"
#include <dji_camera_manager.h>
#include <dji_fc_subscription.h>
#include <dji_flight_controller.h>
#include <dji_hms_manager.h>
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
		// 从 PSDK 日志行解析 SDK CC 序列号 ("Get DJI SDK CC serial num: <SN> (...)" 格式)。
		// 该行由飞控在鉴权阶段主动上报, 基础权限下同样可得, 作为设备标识的兜底真实来源
		_NODISCARD _STD string parseSdkCcSerial(_STD string_view message) noexcept
		{
			constexpr _STD string_view kMarker { "Get DJI SDK CC serial num:" };
			const auto				   position { message.find(kMarker) };
			if (position == _STD string_view::npos)
			{
				return {};
			}

			auto begin { position + kMarker.size() };
			while (begin < message.size() && message[begin] == ' ')
			{
				++begin;
			}
			auto end { begin };
			while (end < message.size() && message[end] != ' ' && message[end] != '(' && message[end] != '\r' && message[end] != '\n')
			{
				++end;
			}
			return _STD string { message.substr(begin, end - begin) };
		}

		// 截取 SDK CC 序列号写入域模型 (仅首次记录; 飞控真序列号读取成功时会覆盖为更高优先级来源)
		void captureSdkCcSerialIfPresent(_STD string_view message) noexcept
		{
			static _STD atomic<bool> captured { false };
			if (captured.load(_STD memory_order_acquire))
			{
				return;
			}

			const _STD string cc_serial { parseSdkCcSerial(message) };
			if (cc_serial.empty())
			{
				return;
			}

			captured.store(true, _STD memory_order_release);
			plane::domain::PlaneStateStore::getInstance().update(
				[&cc_serial](plane::domain::PlaneStateDataClass& st)
				{
					if (st.serial_number.empty())
					{
						st.serial_number = cc_serial;
					}
					if (st.swarm_agent_identifier.empty())
					{
						st.swarm_agent_identifier = _FMT format("swarm.agent.{}", cc_serial);
					}
				}
			);
			LOG_INFO("已从 PSDK 鉴权日志获取 SDK CC 序列号: {} (设备标识兜底来源)", cc_serial);
		}

		// 将 PSDK 日志重定向到 spdlog
		_DJI T_DjiReturnCode psdkLogRedirectCallback(const _STD uint8_t* data, _STD uint16_t dataLen)
		{
			_STD string message(reinterpret_cast<const char*>(data), dataLen);
			while (!message.empty() && (message.back() == '\n' || message.back() == '\r'))
			{
				message.pop_back();
			}

			captureSdkCcSerialIfPresent(message); // SDK CC 序列号 (设备标识兜底)

			plane::utils::Logger::getInstance().PSDKLogRedirection(message);
			return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
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

	void PSDKManager::redirectPsdkLogs(void) noexcept
	{
		static _STD atomic<bool> redirected { false };
		if (redirected.load())
		{
			return;
		}

		const auto& config { plane::config::ConfigManager::getInstance() };

		if (_DJI T_DjiLoggerConsole console = { .func			= _UNNAMED psdkLogRedirectCallback,
												.consoleLevel	= static_cast<uint8_t>(config.getPsdkLogLevel()),
												.isSupportColor = true };
			_DJI DjiLogger_AddConsole(&console) != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
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
		catch (const _STD exception& e)
		{
			LOG_ERROR("PSDKManager 析构异常: {}", e.what());
		}
		catch (...)
		{
			LOG_ERROR("PSDKManager 析构发生未知异常: <non-std exception>");
		}
	}

	bool PSDKManager::start(int argc, char* argv[])
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
			this->hms_initialized_			   = false;
			this->camera_initialized_		   = false;
			this->fc_initialized_			   = false;
			this->fc_subscription_initialized_ = false;
			this->adapter_subscribed_		   = false;

			// 重定向 PSDK 日志到 spdlog (幂等; 入口通常已提前调用, 此处兜底)
			this->redirectPsdkLogs();

			LOG_INFO("DJI PSDK Application 初始化完成");

			// 初始化 HMS 模块
			if (_DJI T_DjiReturnCode returnCode {
					_DJI DjiHmsManager_Init(),
				};
				returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
			{
				LOG_WARN("HMS 模块初始化失败, 错误: {}", plane::utils::convertDjiError(returnCode));
			}
			else
			{
				this->hms_initialized_ = true;
				LOG_INFO("HMS 模块初始化完成");
			}

			// 初始化相机模块 (读取相机固件/激光测距等; 无相机时失败仅告警)
			if (_DJI T_DjiReturnCode returnCode { _DJI DjiCameraManager_Init() }; returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
			{
				LOG_WARN("相机模块初始化失败 (无相机?), 错误: {}", plane::utils::convertDjiError(returnCode));
			}
			else
			{
				this->camera_initialized_ = true;
				LOG_INFO("相机模块初始化完成");
			}

			// 初始化飞控模块 (必须先初始化再调用任何 DjiFlightController_* API, 否则模块未就绪会崩溃)
			// ridInfo: RID 合规要求上报"真实起降点"; 由部署配置提供 (plane.takeoff_lat/lon/alt, 单位: 度/米);
			// 未配置时上报 0 并告警 (绝不使用任何样例坐标)
			_DJI T_DjiFlightControllerRidInfo ridInfo {};
			const double					  takeoff_lat_deg { config.getTakeoffLatitudeDeg() };
			const double					  takeoff_lon_deg { config.getTakeoffLongitudeDeg() };
			const double					  takeoff_alt_m { config.getTakeoffAltitudeM() };
			if (takeoff_lat_deg == 0.0 && takeoff_lon_deg == 0.0)
			{
				LOG_WARN("未配置 'plane.takeoff_lat/lon/alt', RID 起降点将上报 0; 请按实际部署位置配置");
			}
			ridInfo.latitude  = takeoff_lat_deg * (1.0 / _DEFINED RAD_TO_DEG); // PSDK 要求弧度
			ridInfo.longitude = takeoff_lon_deg * (1.0 / _DEFINED RAD_TO_DEG);
			ridInfo.altitude  = static_cast<_STD uint16_t>(takeoff_alt_m);
			if (_DJI T_DjiReturnCode returnCode { _DJI DjiFlightController_Init(ridInfo) };
				returnCode != _DJI	 DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
			{
				LOG_ERROR("飞控模块初始化失败, 错误: {}", plane::utils::convertDjiError(returnCode));
				return false;
			}
			this->fc_initialized_ = true;
			LOG_INFO("飞控模块初始化完成");

			// 初始化数据订阅模块 (官方要求: 订阅任何主题之前先初始化)
			if (_DJI T_DjiReturnCode returnCode { _DJI DjiFcSubscription_Init() }; returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
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
				if (_DJI T_DjiReturnCode returnCode {
						_DJI DjiFlightController_SetRCLostActionEnableStatus(_DJI DJI_FLIGHT_CONTROLLER_DISABLE_RC_LOST_ACTION),
					};
					returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
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
		catch (const _STD exception& e)
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
			if (_DJI T_DjiReturnCode returnCode { _DJI DjiFcSubscription_DeInit() }; returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
			{
				LOG_WARN("数据订阅模块反初始化失败, 错误: {}", plane::utils::convertDjiError(returnCode));
			}
			this->fc_subscription_initialized_ = false;
		}

		if (this->fc_initialized_)
		{
			if (_DJI T_DjiReturnCode returnCode { _DJI DjiFlightController_DeInit() }; returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
			{
				LOG_WARN("飞控模块反初始化失败, 错误: {}", plane::utils::convertDjiError(returnCode));
			}
			this->fc_initialized_ = false;
		}

		if (this->hms_initialized_)
		{
			if (_DJI T_DjiReturnCode returnCode { _DJI DjiHmsManager_DeInit() }; returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
			{
				LOG_WARN("HMS 模块反初始化失败, 错误: {}", plane::utils::convertDjiError(returnCode));
			}
			this->hms_initialized_ = false;
		}

		if (this->camera_initialized_)
		{
			if (_DJI T_DjiReturnCode returnCode { _DJI DjiCameraManager_DeInit() }; returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
			{
				LOG_WARN("相机模块反初始化失败, 错误: {}", plane::utils::convertDjiError(returnCode));
			}
			this->camera_initialized_ = false;
		}

		LOG_INFO("DJI PSDK Application 已反初始化");
		LOG_INFO("--- PSDK 底层服务反初始化完成 ---");
	}
} // namespace plane::manager

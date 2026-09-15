// cy_psdk/manager/psdk/PSDKManager.h

#pragma once

#include "dji_typedef.h"

#include <atomic>
#include <memory>

#include "define.h"

class Application;

namespace plane::manager
{
    class PSDKManager
    {
    public:
        static PSDKManager& getInstance(void) noexcept;

        // 将 PSDK 日志重定向到 spdlog (幂等); 建议在 DjiCore_Init 之前调用, 以便 CORE 初始化阶段的日志可见
        void redirectPsdkLogs(void) noexcept;

        // 启动 PSDK 底层服务，这是一个幂等的操作
        [[nodiscard]] bool start(void);

        // 停止 PSDK 底层服务，这是一个幂等的操作
        void stop(void);

        // 相机模块是否初始化成功 (未成功时不得调用相机接口, 如激光测距轮询)
        [[nodiscard]] bool isCameraInitialized(void) const noexcept;

        // HMS 模块是否初始化成功 (未成功时跳过信息回调注册)
        [[nodiscard]] bool isHmsInitialized(void) const noexcept;

        // 许可等级: PSDK 无公开 license 查询 API, 故以"相机管理 (高级功能) 初始化结果"探测:
        //   成功 → ADVANCED; 返回 NONSUPPORT (Invalid license) → BASIC; 其它错误 → UNKNOWN。
        // 高级功能清单: 相机管理 / 取流(liveview) / 云台管理 / 运动规划(Waypoint V3)
        enum class LicenseLevel
        {
            UNKNOWN = 0, // 判定失败 (非许可类错误)
            BASIC,       // 基础许可
            ADVANCED     // 高级许可
        };

        // 当前许可等级 (start() 时探测)
        [[nodiscard]] LicenseLevel licenseLevel(void) const noexcept;

        // 是否具备高级许可 (高级功能可用性总开关)
        [[nodiscard]] bool isAdvancedLicenseAvailable(void) const noexcept;

        // 取流(liveview)模块是否初始化成功 (高级功能; 未成功时不得调用取流接口)
        [[nodiscard]] bool isLiveviewInitialized(void) const noexcept;

        // 云台管理模块是否初始化成功 (高级功能; 未成功时不得调用云台接口)
        [[nodiscard]] bool isGimbalManagerInitialized(void) const noexcept;

    private:
        explicit PSDKManager(void) noexcept;
        ~PSDKManager(void) noexcept;
        PSDKManager(const PSDKManager&)                   = delete;
        PSDKManager&        operator=(const PSDKManager&) = delete;

        ::std::atomic<bool> running_ { false };

        // 各模块初始化状态: 仅当对应模块初始化成功时才允许反初始化, 避免对未就绪模块调用 SDK 接口导致崩溃
        bool hms_initialized_ { false };
        bool camera_initialized_ { false };
        bool liveview_initialized_ { false };
        bool gimbal_manager_initialized_ { false };
        bool fc_initialized_ { false };
        bool fc_subscription_initialized_ { false };
        bool adapter_subscribed_ { false };

        // 许可等级 (探测式判定, 见 LicenseLevel 说明)
        LicenseLevel license_level_ { LicenseLevel::UNKNOWN };
    };
} // namespace plane::manager

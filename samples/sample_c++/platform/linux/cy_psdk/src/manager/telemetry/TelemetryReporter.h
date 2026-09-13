// cy_psdk/manager/telemetry/TelemetryReporter.h
//
// 上报节拍自治: STATUS 固定 10Hz / FIXED_INFO 固定 1Hz, 由本组件自持定时线程驱动,
// 与 PSDK 采集频率、心跳服务等任何外部事件解耦 (数据的新旧/真假不由发送者评判)。

#pragma once

#include "manager/event_manager/EventManager.h"
#include "protocol/HeartbeatDataClass.h"

#include <BS_thread_pool.hpp>
#include <eventpp/eventdispatcher.h>
#include <eventpp/utilities/scopedremover.h>

#include <string_view>
#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include "define.h"

namespace plane::manager
{
    class TelemetryReporter
    {
    public:
        static TelemetryReporter& getInstance(void) noexcept;

        // 启动遥测上报服务
        [[nodiscard]] bool start(void);

        // 停止遥测上报服务
        void stop(void);

    private:
        explicit TelemetryReporter(void) noexcept;
        ~TelemetryReporter(void) noexcept;
        TelemetryReporter(const TelemetryReporter&) noexcept            = delete;
        TelemetryReporter& operator=(const TelemetryReporter&) noexcept = delete;

        // 发布 JSON 字符串到 MQTT 主题
        [[nodiscard]] bool publishJson(::std::string_view topic, ::std::string_view statusJson) noexcept;

        // PSDK 事件处理相关
        void onPSDKEvent(const plane::manager::EventManager::PSDKEventData& eventData);

        // 上报节拍线程: STATUS 固定 10Hz, 每第 FIXED_INFO_EVERY_N_TICKS 拍附发一次 FIXED_INFO (1Hz)
        void runReportLoop(void) noexcept;
        void publishStatusReport(void) noexcept;
        void publishFixedInfo(void) noexcept;

        // 启动看门狗检查
        void                                                                                        runWatchdogCheck(void) noexcept;

        ::std::unique_ptr<::eventpp::ScopedRemover<plane::manager::EventManager::StatusDispatcher>> psdk_event_remover_ {};
        ::std::unique_ptr<::BS::thread_pool<>>                                                      event_processing_pool_ {};
        ::std::thread                                                                               report_thread_ {};
        ::std::atomic<bool>                                                                         run_watchdog_ { false };
        ::std::atomic<bool>                                                                         running_ { false };
        ::std::atomic<::std::chrono::steady_clock::time_point>                                      last_health_ping_time_ {};
        ::std::atomic<::std::size_t>                                                                queued_task_count_ { 0 };
        constexpr static auto STATUS_REPORT_INTERVAL { ::std::chrono::milliseconds(100) }; // STATUS 上报周期 (10Hz)
        constexpr static auto FIXED_INFO_EVERY_N_TICKS { 10 };                             // 每 10 拍附发一次固定信息 (1Hz)
        constexpr static auto PSDK_WATCHDOG_CHECK_INTERVAL { ::std::chrono::seconds(1) };
        constexpr static auto MAX_EVENT_QUEUE_SIZE { 100 };
        constexpr static auto PSDK_WATCHDOG_TIMEOUT { ::std::chrono::seconds(1) };
    };
} // namespace plane::manager

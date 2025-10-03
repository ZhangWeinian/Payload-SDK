// raspberry_pi/services/Telemetry/TelemetryReporter.h

#pragma once

#include "protocol/HeartbeatDataClass.h"
#include "services/DroneControl/PSDKAdapter/PSDKAdapter.h"
#include "services/EventManager/EventManager.h"

#include <eventpp/eventdispatcher.h>
#include <ThreadPool/ThreadPool.h>

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

namespace plane::services
{
	class TelemetryReporter
	{
	public:
		static TelemetryReporter& getInstance(void) noexcept;
		_NODISCARD bool			  start(void);
		void					  stop(void);

	private:
		explicit TelemetryReporter(void) noexcept;
		~TelemetryReporter(void) noexcept;
		TelemetryReporter(const TelemetryReporter&) noexcept			= delete;
		TelemetryReporter& operator=(const TelemetryReporter&) noexcept = delete;

		bool			   publishJson(_STD string_view topic, _STD string_view statusJson) noexcept;
		void			   onPsdkEvent(const plane::services::EventManager::PSDKEventData& eventData);
		void			   runWatchdogCheck(void) noexcept;

		_STD unique_ptr<_EVENTPP ScopedRemover<plane::services::EventManager::StatusDispatcher>> removers_ {};
		_STD unique_ptr<_THREADPOOL ThreadPool> event_processing_pool_ {};
		_STD atomic<bool>	  run_watchdog_ { false };
		constexpr static auto PSDK_WATCHDOG_TIMEOUT { _STD_CHRONO seconds(3) };
		constexpr static auto PSDK_WATCHDOG_CHECK_INTERVAL { _STD_CHRONO seconds(1) };
		constexpr static auto MAX_EVENT_QUEUE_SIZE { 100 };
		_STD atomic<_STD size_t> queued_task_count_ { 0 };
	};
} // namespace plane::services

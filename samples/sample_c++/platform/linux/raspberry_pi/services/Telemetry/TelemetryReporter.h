// raspberry_pi/services/Telemetry/TelemetryReporter.h

#pragma once

#include "protocol/HeartbeatDataClass.h"
#include "services/DroneControl/PSDKAdapter/PSDKAdapter.h"

#include <eventpp/eventdispatcher.h>

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
		explicit TelemetryReporter(void) noexcept = default;
		~TelemetryReporter(void) noexcept;
		TelemetryReporter(const TelemetryReporter&) noexcept			= delete;
		TelemetryReporter& operator=(const TelemetryReporter&) noexcept = delete;

		bool publishJson(_STD string_view topic, _STD string_view status_json) noexcept;
		void onPsdkEvent(const plane::services::PSDKAdapter::EventData& eventData);

		_STD unique_ptr<_EVENTPP ScopedRemover<plane::services::PSDKAdapter::EventDispatcher>> removers_ {};
		constexpr static auto																   PSDK_WATCHDOG_TIMEOUT { _STD_CHRONO seconds(3) };
	};
} // namespace plane::services

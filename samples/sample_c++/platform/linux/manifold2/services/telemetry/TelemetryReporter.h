// manifold2/services/telemetry/TelemetryReporter.h

#pragma once

#include "define.h"

#include <string_view>
#include <atomic>
#include <string>
#include <thread>

namespace plane::services
{
	class TelemetryReporter
	{
	public:
		static TelemetryReporter& getInstance(void) noexcept;
		_NODISCARD bool			  start(void) noexcept;
		void					  stop(void) noexcept;

	private:
		void		statusReportLoop(void) noexcept;
		void		fixedInfoReportLoop(void) noexcept;
		bool		publishStatus(_STD string_view topic, _STD string_view status_json) noexcept;

		_STD thread status_thread_ {};
		_STD thread fixed_info_thread_ {};
		_STD atomic<bool> run_ { false };

		explicit TelemetryReporter(void) noexcept = default;
		~TelemetryReporter(void) noexcept;
		TelemetryReporter(const TelemetryReporter&) noexcept			= delete;
		TelemetryReporter& operator=(const TelemetryReporter&) noexcept = delete;
	};
} // namespace plane::services

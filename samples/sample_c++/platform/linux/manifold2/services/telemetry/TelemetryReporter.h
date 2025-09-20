#pragma once

#include <atomic>
#include <thread>

namespace plane::services
{
	class TelemetryReporter
	{
	public:
		static TelemetryReporter& getInstance(void) noexcept;
		bool					  start(void) noexcept;
		void					  stop(void) noexcept;

	private:
		void			  statusReportLoop(void) noexcept;
		void			  fixedInfoReportLoop(void) noexcept;

		std::thread		  status_thread_ {};
		std::thread		  fixed_info_thread_ {};
		std::atomic<bool> run_ { false };

		TelemetryReporter(void) = default;
		~TelemetryReporter(void) noexcept;
		TelemetryReporter(const TelemetryReporter&)			   = delete;
		TelemetryReporter& operator=(const TelemetryReporter&) = delete;
	};
} // namespace plane::services

#pragma once

#include <string_view>
#include <atomic>
#include <string>
#include <thread>

namespace plane::services
{
#ifndef _NODISCARD
	#define _NODISCARD [[nodiscard]]
#endif

	class TelemetryReporter
	{
	public:
		static TelemetryReporter& getInstance(void) noexcept;
		_NODISCARD bool			  start(void) noexcept;
		void					  stop(void) noexcept;

	private:
		void			  statusReportLoop(void) noexcept;
		void			  fixedInfoReportLoop(void) noexcept;
		bool			  publishStatus(std::string_view topic, std::string_view status_json) noexcept;

		std::thread		  status_thread_ {};
		std::thread		  fixed_info_thread_ {};
		std::atomic<bool> run_ { false };

		explicit TelemetryReporter(void) noexcept = default;
		~TelemetryReporter(void) noexcept;
		TelemetryReporter(const TelemetryReporter&) noexcept			= delete;
		TelemetryReporter& operator=(const TelemetryReporter&) noexcept = delete;
	};
} // namespace plane::services

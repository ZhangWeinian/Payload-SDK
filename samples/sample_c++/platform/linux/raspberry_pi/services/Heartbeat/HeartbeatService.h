// raspberry_pi/services/Heartbeat/HeartbeatService.h
#pragma once

#include <atomic>
#include <chrono>
#include <thread>

#include "define.h"

namespace plane::services
{
	class HeartbeatService
	{
	public:
		static HeartbeatService& getInstance(void) noexcept;

		_NODISCARD bool			 start(_STD_CHRONO milliseconds interval = _STD_CHRONO seconds(1));
		void					 stop(void);

	private:
		explicit HeartbeatService(void) noexcept = default;
		~HeartbeatService(void) noexcept;
		HeartbeatService(const HeartbeatService&)			 = delete;
		HeartbeatService& operator=(const HeartbeatService&) = delete;

		void			  runLoop(_STD_CHRONO milliseconds interval);

		_STD thread		  heartbeat_thread_ {};
		_STD atomic<bool> run_heartbeat_ { false };
		_STD atomic<bool> stopped_ { false };
	};
} // namespace plane::services

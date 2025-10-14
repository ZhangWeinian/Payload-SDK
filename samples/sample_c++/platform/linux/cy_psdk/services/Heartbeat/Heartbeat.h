// cy_psdk/services/Heartbeat/Heartbeat.h
#pragma once

#include <atomic>
#include <chrono>
#include <thread>

#include "define.h"

namespace plane::services
{
	class Heartbeat
	{
	public:
		static Heartbeat& getInstance(void) noexcept;

		// 启动心跳服务，interval 参数指定心跳间隔，默认为 1 秒
		_NODISCARD bool start(_STD_CHRONO milliseconds interval = _STD_CHRONO seconds(1));

		// 停止心跳服务
		void stop(void);

	private:
		explicit Heartbeat(void) noexcept = default;
		~Heartbeat(void) noexcept;
		Heartbeat(const Heartbeat&)				= delete;
		Heartbeat&	operator=(const Heartbeat&) = delete;

		void		runLoop(_STD_CHRONO milliseconds interval);

		_STD thread heartbeat_thread_ {};
		_STD atomic<bool> running_ { false };
	};
} // namespace plane::services

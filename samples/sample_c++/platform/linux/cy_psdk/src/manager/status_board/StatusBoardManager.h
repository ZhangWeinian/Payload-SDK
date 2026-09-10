// cy_psdk/manager/status_board/StatusBoardManager.h
//
// 状态板管理: 周期性把 PlaneStateStore 中的关键运行状态
// (MQTT / WebSocket / Catalog / 绑定 / 序列号) 同步到终端状态板。

#pragma once

#include <atomic>
#include <chrono>
#include <thread>

#include "define.h"

namespace plane::manager
{
	class StatusBoardManager
	{
	public:
		static StatusBoardManager& getInstance(void) noexcept;

		// 启动状态同步服务, interval 指定刷新间隔, 默认为 1 秒 (幂等)
		_NODISCARD bool start(_STD_CHRONO milliseconds interval = _STD_CHRONO seconds(1));

		// 停止状态同步服务 (幂等)
		void stop(void);

	private:
		explicit StatusBoardManager(void) noexcept = default;
		~StatusBoardManager(void) noexcept;
		StatusBoardManager(const StatusBoardManager&)			 = delete;
		StatusBoardManager& operator=(const StatusBoardManager&) = delete;

		void				runLoop(_STD_CHRONO milliseconds interval);

		// 从 PlaneStateStore 读取最新状态并同步到状态板
		void		refreshStatus(void);

		_STD thread status_thread_ {};
		_STD atomic<bool> running_ { false };
	};
} // namespace plane::manager

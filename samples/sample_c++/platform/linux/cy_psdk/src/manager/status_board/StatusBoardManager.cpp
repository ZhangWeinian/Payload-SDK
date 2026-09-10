// cy_psdk/manager/status_board/StatusBoardManager.cpp

#include "manager/status_board/StatusBoardManager.h"

#include <system_error>

#include "manager/plane_state/PlaneStateStore.h"
#include "utils/status_board/StatusBoard.h"

namespace plane::manager
{
	StatusBoardManager& StatusBoardManager::getInstance(void) noexcept
	{
		static StatusBoardManager instance {};
		return instance;
	}

	StatusBoardManager::~StatusBoardManager(void) noexcept
	{
		this->stop();
	}

	bool StatusBoardManager::start(_STD_CHRONO milliseconds interval)
	{
		if (running_)
		{
			return true; // 幂等
		}
		try
		{
			running_	   = true;
			status_thread_ = _STD thread(&StatusBoardManager::runLoop, this, interval);
		}
		catch (const _STD system_error&)
		{
			running_ = false; // 线程创建失败 (资源不足等): 报告失败, 不影响主流程
			return false;
		}
		return true;
	}

	void StatusBoardManager::stop(void)
	{
		running_ = false;
		if (status_thread_.joinable())
		{
			status_thread_.join();
		}
	}

	void StatusBoardManager::runLoop(_STD_CHRONO milliseconds interval)
	{
		while (running_)
		{
			this->refreshStatus();
			_STD this_thread::sleep_for(interval);
		}
	}

	// 从 PlaneStateStore 读取最新状态并同步到终端状态板
	// 注: 状态板按"首次 update 的顺序"排列, 此处决定最终展示顺序 (Catalog 置首)
	void StatusBoardManager::refreshStatus(void)
	{
		const auto state { plane::domain::PlaneStateStore::getInstance().snapshot() };
		auto&	   board { plane::utils::StatusBoard::getInstance() };

		// SwarmCatalog 目录状态 (置首展示)
		if (state.catalog_ready)
		{
			board.update("Catalog", state.catalog_state + " (就绪)", plane::utils::StatusLevel::Ok);
		}
		else
		{
			board.update("Catalog", state.catalog_state.empty() ? "初始化中…" : state.catalog_state, plane::utils::StatusLevel::Warn);
		}

		// MQTT 连接状态 (已连接时附带地址; 地址未知则只显示连接状态)
		if (state.mqtt_connected)
		{
			const _STD string value { state.mqtt_connected_url.empty() ? _STD string { "✔ 已连接" }
																	   : "✔ 已连接 (" + state.mqtt_connected_url + ")" };
			board.update("MQTT", value, plane::utils::StatusLevel::Ok);
		}
		else
		{
			board.update("MQTT", "未连接", plane::utils::StatusLevel::Warn);
		}

		// WebSocket 连接状态
		board.update(
			"WebSocket",
			state.web_socket_connected ? "✔ 已连接" : "未连接",
			state.web_socket_connected ? plane::utils::StatusLevel::Ok : plane::utils::StatusLevel::Warn
		);

		// 设备绑定状态
		board.update(
			"Bind",
			state.device_binding ? "✔ 已绑定" : "未绑定",
			state.device_binding ? plane::utils::StatusLevel::Ok : plane::utils::StatusLevel::Warn
		);

		// 飞控序列号
		board.update("Serial", state.serial_number.empty() ? "—" : state.serial_number);
	}
} // namespace plane::manager

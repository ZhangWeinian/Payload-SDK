// cy_psdk/services/EventManager/EventManager.cpp

#include "EventManager.h"

namespace plane::services
{
	EventManager& EventManager::getInstance(void) noexcept
	{
		static EventManager instance {};
		return instance;
	}

	void EventManager::publishCommand(CommandEvent event, const CommandData& data)
	{
		LOG_DEBUG("发布命令事件: {}，负载类型: {}", static_cast<int>(event), data.index());
		if (!this->is_full_psdk_)
		{
			LOG_WARN("没有启用标准 PSDK 程序，发布的【命令事件】可能不会有后文。");
		}
		this->command_queue_.enqueue(event, data);
	}

	void EventManager::publishStatus(PSDKEvent event, const PSDKEventData& data)
	{
		LOG_DEBUG("发布状态事件: {}，负载类型: {}", static_cast<int>(event), data.index());
		if (!this->is_full_psdk_)
		{
			LOG_WARN("没有启用标准 PSDK 程序，发布的【状态事件】可能不会有后文。");
		}
		this->status_dispatcher_.dispatch(event, data);
	}

	void EventManager::publishSystemEvent(SystemEvent event, const SystemEventData& data)
	{
		// 系统事件和 PSDK 程序无关，始终发布
		LOG_DEBUG("发布系统事件: {}，负载类型: {}", static_cast<int>(event), data.index());
		this->system_dispatcher_.dispatch(event, data);
	}
} // namespace plane::services

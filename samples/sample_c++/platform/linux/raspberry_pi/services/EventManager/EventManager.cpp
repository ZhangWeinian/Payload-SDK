// raspberry_pi/services/EventManager/EventManager.cpp

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
		command_queue_.enqueue(event, data);
	}

	void EventManager::publishStatus(PSDKEvent event, const PSDKEventData& data)
	{
		status_dispatcher_.dispatch(event, data);
	}

	void EventManager::publishSystemEvent(SystemEvent event, const SystemEventData& data)
	{
		system_dispatcher_.dispatch(event, data);
	}
} // namespace plane::services

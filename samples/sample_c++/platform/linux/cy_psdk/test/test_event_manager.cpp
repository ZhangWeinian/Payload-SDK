// cy_psdk/test/test_event_manager.cpp
//
// EventManager 系统事件单元测试:
//   - MqttBrokerUpdated 携带 broker URL 数据并同步派发 (catalog -> MQTT 解耦通道)
//   - HeartbeatTick (monostate 数据) 兼容派发
// 说明: SystemDispatcher 为同步派发, 测试内在发布后直接断言回调结果。

#include <gtest/gtest.h>

#include <eventpp/utilities/scopedremover.h>

#include <string>

#include "define.h"
#include "manager/event_manager/EventManager.h"

namespace
{
	using plane::manager::EventManager;

	TEST(EventManagerTest, SystemEventsDispatchWithData)
	{
		auto&		event_manager { EventManager::getInstance() };
		auto&		dispatcher { event_manager.getSystemDispatcher() };

		_STD string broker_url {};
		int			heartbeat_count { 0 };

		_EVENTPP ScopedRemover<EventManager::SystemDispatcher> remover { dispatcher };
		remover.appendListener(
			EventManager::SystemEvent::MqttBrokerUpdated,
			[&broker_url](const EventManager::SystemEventData& data)
			{
				if (const auto* url { _STD get_if<_STD string>(&data) })
				{
					broker_url = *url;
				}
			}
		);
		remover.appendListener(
			EventManager::SystemEvent::HeartbeatTick,
			[&heartbeat_count](const EventManager::SystemEventData&)
			{
				++heartbeat_count;
			}
		);

		event_manager.publishSystemEvent(EventManager::SystemEvent::MqttBrokerUpdated, _STD string { "tcp://10.1.2.3:1883" });
		EXPECT_EQ(broker_url, "tcp://10.1.2.3:1883");

		event_manager.publishSystemEvent(EventManager::SystemEvent::HeartbeatTick);
		event_manager.publishSystemEvent(EventManager::SystemEvent::HeartbeatTick);
		EXPECT_EQ(heartbeat_count, 2);

		// 空/缺省数据不得导致崩溃 (monostate 分支)
		event_manager.publishSystemEvent(EventManager::SystemEvent::MqttBrokerUpdated);
		EXPECT_EQ(broker_url, "tcp://10.1.2.3:1883"); // 未被空数据覆盖
	}
} // namespace

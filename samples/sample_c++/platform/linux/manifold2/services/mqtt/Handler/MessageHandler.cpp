// manifold2/services/mqtt/Handler/MessageHandler.cpp

#include "services/mqtt/Handler/MessageHandler.h"

#include "utils/Logger/Logger.h"

namespace plane::services
{
	MqttMessageHandler& MqttMessageHandler::getInstance(void) noexcept
	{
		static MqttMessageHandler instance {};
		return instance;
	}

	void MqttMessageHandler::registerHandler(_STD string_view topic, _STD string_view messageType, LogicHandler handler) noexcept
	{
		_STD lock_guard<_STD mutex>				lock(handler_mutex_);
		handler_map_[topic][messageType] = _STD move(handler);
		LOG_DEBUG("为主题 '{}', 消息类型 '{}' 注册了处理器。", topic, messageType);
	}

	void MqttMessageHandler::routeMessage(_STD string_view topic, _STD string_view messageType, const n_json& payloadJson) noexcept
	{
		try
		{
			_STD lock_guard<_STD mutex> lock(handler_mutex_);
			if (auto topic_it { handler_map_.find(topic) }; topic_it != handler_map_.end())
			{
				if (auto type_it { topic_it->second.find(messageType) }; type_it != topic_it->second.end())
				{
					LOG_DEBUG("路由消息: topic='{}', messageType='{}'", topic, messageType);
					type_it->second(payloadJson);
				}
				else
				{
					LOG_WARN("主题 '{}' 下收到未注册的消息类型 '{}'。", topic, messageType);
				}
			}
			else
			{
				LOG_WARN("收到未注册主题 '{}' 的消息。", topic);
			}
		}
		catch (const _STD exception& e)
		{
			LOG_ERROR("处理 MQTT 业务逻辑时发生错误 (topic: {}, type: {}): {}", topic, messageType, e.what());
		}
	}
} // namespace plane::services

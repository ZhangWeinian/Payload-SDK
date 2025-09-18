#include "services/mqtt/MqttMessageHandler/MqttMessageHandler.h"
#include "utils/Logger/Logger.h"

namespace plane::services
{
	MqttMessageHandler& MqttMessageHandler::getInstance()
	{
		static MqttMessageHandler instance {};
		return instance;
	}

	void MqttMessageHandler::registerHandler(const std::string& topic, const std::string& messageType, HandlerFunc handler)
	{
		std::lock_guard<std::mutex> lock(handler_mutex_);
		handler_map_[topic][messageType] = std::move(handler);
		LOG_INFO("为主题 '{}', 消息类型 '{}' 注册了处理器。", topic, messageType);
	}

	void MqttMessageHandler::routeMessage(const std::string& topic, const std::string& rawJsonPayload)
	{
		try
		{
			n_json						jValue { n_json::parse(rawJsonPayload) };
			std::string					messageType { jValue.at("XXLX").get<std::string>() };
			const n_json&				payloadJson { jValue.at("XXXX") };
			std::lock_guard<std::mutex> lock(handler_mutex_);

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
		catch (const n_json::exception& e)
		{
			LOG_ERROR("解析 MQTT 消息 JSON 失败 (topic: {}): {}", topic, e.what());
		}
		catch (const std::exception& e)
		{
			LOG_ERROR("处理 MQTT 消息时发生未知错误 (topic: {}): {}", topic, e.what());
		}
	}
} // namespace plane::services

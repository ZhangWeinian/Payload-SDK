#include "config/ConfigManager.h"
#include "services/mqtt/MqttService.h"
#include "services/mqtt/MqttTopics.h"

#include "MQTTAsync.h"

#include <dji_logger.h>

// Impl 结构体定义，包含了所有私有成员变量
struct plane::MQTTService::Impl
{
	MQTTAsync		   client = nullptr;
	std::string		   serverURI;
	std::string		   clientId;
	bool			   connected = false;
	mutable std::mutex connection_mutex;

	~Impl()
	{
		if (client)
		{
			MQTTAsync_destroy(&client);
		}
	}
};

namespace
{
	// 连接成功时的回调
	void onConnectSuccess(void* context, MQTTAsync_successData* response)
	{
		plane::MQTTService* service = static_cast<plane::MQTTService*>(context);
		USER_LOG_INFO("MQTT connection successful!");

		service->subscribe(plane::services::mqtt::TOPIC_MISSION_CONTROL);
		service->subscribe(plane::services::mqtt::TOPIC_COMMAND_CONTROL);
		service->subscribe(plane::services::mqtt::TOPIC_PAYLOAD_CONTROL);
		service->subscribe(plane::services::mqtt::TOPIC_ROCKER_CONTROL);
		service->subscribe(plane::services::mqtt::TOPIC_VELOCITY_CONTROL);
	}

	// 连接失败时的回调
	void onConnectFailure(void* context, MQTTAsync_failureData* response)
	{
		USER_LOG_ERROR("MQTT connection failed, error code: %d, message: %s",
					   response ? response->code : -1,
					   response ? response->message : "unknown error");
	}

	// 连接丢失时的回调
	void connectionLost(void* context, char* cause)
	{
		USER_LOG_WARN("MQTT connection lost, cause: %s. Will auto-reconnect.", cause ? cause : "unknown");
	}

	// 收到消息时的回调
	int messageArrived(void* context, char* topicName, int topicLen, MQTTAsync_message* message)
	{
		std::string topic(topicName, topicLen > 0 ? topicLen : strlen(topicName));
		std::string payload(static_cast<char*>(message->payload), message->payloadlen);

		// 直接用日志打印收到的消息，而不是发给 MessageCenter
		USER_LOG_INFO("MQTT Message Arrived! Topic: [%s], Payload: [%s]", topic.c_str(), payload.c_str());

		MQTTAsync_freeMessage(&message);
		MQTTAsync_free(topicName);

		return 1;
	}

	// 消息发布完成时的回调
	void deliveryComplete(void* context, MQTTAsync_token token)
	{
		// USER_LOG_DEBUG("MQTT message delivery complete, token: %d", token);
	}
} // namespace

// 初始化工作
plane::MQTTService::MQTTService(): impl_(new Impl())
{
	// 1. 从 ConfigManager 获取配置
	impl_->serverURI = ConfigManager::getInstance().getMqttUrl();
	impl_->clientId	 = ConfigManager::getInstance().getMqttClientId();

	if (impl_->serverURI.empty() || impl_->clientId.empty())
	{
		USER_LOG_ERROR("MQTT config is empty! Please check config.yml.");
		return;
	}

	USER_LOG_INFO("Creating MQTT client for server [%s] with clientID [%s]", impl_->serverURI.c_str(), impl_->clientId.c_str());

	// 2. 创建 MQTT 异步客户端
	int rc = MQTTAsync_create(&impl_->client, impl_->serverURI.c_str(), impl_->clientId.c_str(), MQTTCLIENT_PERSISTENCE_NONE, nullptr);
	if (rc != MQTTASYNC_SUCCESS)
	{
		USER_LOG_ERROR("Failed to create MQTT client, return code %d", rc);
		return;
	}

	// 3. 设置回调函数
	MQTTAsync_setCallbacks(impl_->client, this, connectionLost, messageArrived, deliveryComplete);

	// 4. 配置连接选项
	MQTTAsync_connectOptions conn_opts = MQTTAsync_connectOptions_initializer;
	conn_opts.keepAliveInterval		   = 20;
	conn_opts.cleansession			   = 1;
	conn_opts.onSuccess				   = onConnectSuccess;
	conn_opts.onFailure				   = onConnectFailure;
	conn_opts.context				   = this;
	conn_opts.automaticReconnect	   = 1;
	conn_opts.minRetryInterval		   = 1;
	conn_opts.maxRetryInterval		   = 60;

	// 5. 发起连接
	rc = MQTTAsync_connect(impl_->client, &conn_opts);
	if (rc != MQTTASYNC_SUCCESS)
	{
		USER_LOG_ERROR("Failed to start MQTT connection, return code %d", rc);
	}
	else
	{
		USER_LOG_INFO("MQTT connection attempt initiated...");
	}
}

plane::MQTTService::~MQTTService()
{
	if (impl_->client)
	{
		MQTTAsync_disconnectOptions opts = MQTTAsync_disconnectOptions_initializer;
		opts.timeout					 = 1000;
		MQTTAsync_disconnect(impl_->client, &opts);
	}
}

// 单例的 getInstance 实现
plane::MQTTService& plane::MQTTService::getInstance()
{
	static MQTTService instance;
	return instance;
}

// publish 函数的实现
void plane::MQTTService::publish(const std::string& topic, const std::string& payload)
{
	if (!isConnected())
	{
		USER_LOG_WARN("MQTT not connected, publish request ignored.");
		return;
	}

	MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
	opts.context				   = this;
	// opts.onSuccess = ...; // 可以为单次发布设置成功/失败回调

	int rc = MQTTAsync_send(impl_->client, topic.c_str(), payload.length(), payload.c_str(), 1, 0, &opts);
	if (rc != MQTTASYNC_SUCCESS)
	{
		USER_LOG_ERROR("Failed to publish message, return code %d", rc);
	}
}

// subscribe 函数的实现
void plane::MQTTService::subscribe(const std::string& topic)
{
	if (!isConnected())
	{
		USER_LOG_WARN("MQTT not connected, subscribe request ignored.");
		return;
	}

	USER_LOG_INFO("Subscribing to topic: %s", topic.c_str());
	MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
	int						  rc   = MQTTAsync_subscribe(impl_->client, topic.c_str(), 1, &opts);
	if (rc != MQTTASYNC_SUCCESS)
	{
		USER_LOG_ERROR("Failed to subscribe to topic %s, return code %d", topic.c_str(), rc);
	}
}

// isConnected 函数的实现
bool plane::MQTTService::isConnected() const
{
	return MQTTAsync_isConnected(impl_->client);
}

#include "config/ConfigManager.h"
#include "services/mqtt/MqttMessageHandler/MqttMessageHandler.h"
#include "services/mqtt/MqttService/MqttService.h"
#include "services/mqtt/MqttTopics.h"
#include "utils/Logger/Logger.h"

#include "fmt/format.h"

#include <chrono>
#include <cstring>
#include <thread>

struct plane::services::mqtt::MQTTService::Impl
{
	MQTTAsync		  client { nullptr };
	std::string		  serverURI {};
	std::string		  clientId {};
	std::thread		  heartbeat_thread_ {};
	std::atomic<bool> run_heartbeat_ { true };

	~Impl() = default;
};

static void onSubscribeSuccess(void* context, MQTTAsync_successData* response)
{
	std::string* topic { static_cast<std::string*>(context) };
	LOG_INFO("成功订阅主题: '{}'", *topic);
	delete topic;
}

static void onSubscribeFailure(void* context, MQTTAsync_failureData* response)
{
	std::string* topic { static_cast<std::string*>(context) };
	LOG_ERROR("订阅主题 '{}' 失败，错误码: {}", *topic, response ? response->code : -1);
	delete topic;
}

void onConnectSuccess(void* context, MQTTAsync_successData* response)
{
	plane::services::mqtt::MQTTService* service { static_cast<plane::services::mqtt::MQTTService*>(context) };
	service->setConnected(true);
	LOG_INFO("MQTT 连接成功!（内部状态更新为已连接）");

	service->subscribe(plane::services::mqtt::TOPIC_MISSION_CONTROL);
	service->subscribe(plane::services::mqtt::TOPIC_COMMAND_CONTROL);
	service->subscribe(plane::services::mqtt::TOPIC_PAYLOAD_CONTROL);
	service->subscribe(plane::services::mqtt::TOPIC_ROCKER_CONTROL);
	service->subscribe(plane::services::mqtt::TOPIC_VELOCITY_CONTROL);
	service->subscribe(plane::services::mqtt::TOPIC_TEST);
}

void onConnectFailure(void* context, MQTTAsync_failureData* response)
{
	plane::services::mqtt::MQTTService* service { static_cast<plane::services::mqtt::MQTTService*>(context) };
	service->setConnected(false);
	LOG_ERROR("MQTT 连接失败，错误码: {}, 消息: {}", response ? response->code : -1, response ? response->message : "未知错误");
}

static int messageArrived(void* context, char* topicName, int topicLen, MQTTAsync_message* message)
{
	std::string topic(topicName, topicLen > 0 ? topicLen : strlen(topicName));
	std::string rawPayload(static_cast<char*>(message->payload), message->payloadlen);
	plane::services::mqtt::MqttMessageHandler::getInstance().routeMessage(topic, rawPayload);
	MQTTAsync_freeMessage(&message);
	MQTTAsync_free(topicName);
	return 1;
}

void connectionLost(void* context, char* cause)
{
	plane::services::mqtt::MQTTService* service { static_cast<plane::services::mqtt::MQTTService*>(context) };
	service->setConnected(false);
	LOG_WARN("MQTT 连接已断开，原因: {}. Paho 库将自动重新连接。", cause ? cause : "未知");
}

static void deliveryComplete(void* context, MQTTAsync_token token)
{
	LOG_DEBUG("MQTT 消息发送完成，令牌: {}", token);
}

plane::services::mqtt::MQTTService::MQTTService(): impl_(new Impl())
{
	impl_->serverURI = config::ConfigManager::getInstance().getMqttUrl();
	impl_->clientId	 = config::ConfigManager::getInstance().getMqttClientId();

	if (impl_->serverURI.empty() || impl_->clientId.empty())
	{
		LOG_ERROR("MQTT 配置为空！正在中止。");
		return;
	}

	LOG_INFO("配置已加载。服务器=[{}]，客户端ID=[{}]", impl_->serverURI, impl_->clientId);

	if (int rc { MQTTAsync_create(&impl_->client, impl_->serverURI.c_str(), impl_->clientId.c_str(), MQTTCLIENT_PERSISTENCE_NONE, nullptr) };
		rc != MQTTASYNC_SUCCESS)
	{
		LOG_ERROR("MQTTAsync_create 失败，返回码 {}", rc);
		return;
	}

	MQTTAsync_setCallbacks(impl_->client, this, connectionLost, messageArrived, deliveryComplete);

	MQTTAsync_connectOptions conn_opts = MQTTAsync_connectOptions_initializer;
	conn_opts.keepAliveInterval		   = 20;
	conn_opts.cleansession			   = 1;
	conn_opts.onSuccess				   = onConnectSuccess;
	conn_opts.onFailure				   = onConnectFailure;
	conn_opts.context				   = this;
	conn_opts.automaticReconnect	   = 1;
	conn_opts.minRetryInterval		   = 1;
	conn_opts.maxRetryInterval		   = 60;

	if (int rc { MQTTAsync_connect(impl_->client, &conn_opts) }; rc != MQTTASYNC_SUCCESS)
	{
		LOG_ERROR("启动 MQTT 连接失败，返回码 {}", rc);
	}
	else
	{
		LOG_INFO("MQTTAsync_connect 调用成功。连接过程正在后台运行。");
	}
}

plane::services::mqtt::MQTTService::~MQTTService()
{
	shutdown();
}

void plane::services::mqtt::MQTTService::startBackgroundThreads(void) noexcept
{
	LOG_INFO("正在启动后台任务线程...");
	impl_->heartbeat_thread_ = std::thread(
		[this]
		{
			this->heartbeatLoop();
		});
	// 如果未来有其他线程，在这里一并启动：
	// impl_->sensor_thread_ = std::thread([this] { this->sensorLoop(); });
}

void plane::services::mqtt::MQTTService::stopBackgroundThreads(void) const noexcept
{
	LOG_INFO("正在停止后台任务线程...");

	impl_->run_heartbeat_.store(false, std::memory_order_release);
	if (impl_->heartbeat_thread_.joinable())
	{
		impl_->heartbeat_thread_.join();
	}

	// 如果未来有其他线程，在这里一并停止：
	// impl_->run_sensor_.store(false, std::memory_order_release);
	// if (impl_->sensor_thread_.joinable())
	// {
	//	   impl_->sensor_thread_.join();
	// }
}

plane::services::mqtt::MQTTService& plane::services::mqtt::MQTTService::getInstance(void) noexcept
{
	static MQTTService instance;
	return instance;
}

void plane::services::mqtt::MQTTService::publish(const std::string& topic, const std::string& payload) noexcept
{
	if (!isConnected())
	{
		LOG_WARN("MQTT 未连接，发布请求被忽略。");
		return;
	}

	MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
	opts.context				   = this;
	if (int rc { MQTTAsync_send(impl_->client, topic.c_str(), payload.length(), payload.c_str(), 1, 0, &opts) }; rc != MQTTASYNC_SUCCESS)
	{
		LOG_ERROR("向主题 '{}' 发布消息失败，返回码 {}", topic, rc);
	}
}

void plane::services::mqtt::MQTTService::subscribe(const std::string& topic) const noexcept
{
	if (!isConnected())
	{
		LOG_WARN("MQTT 未连接，对主题 '{}' 的订阅请求被忽略。", topic);
		return;
	}

	MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
	opts.onSuccess				   = onSubscribeSuccess;
	opts.onFailure				   = onSubscribeFailure;
	opts.context				   = new std::string(topic);

	if (int rc { MQTTAsync_subscribe(impl_->client, topic.c_str(), 1, &opts) }; rc != MQTTASYNC_SUCCESS)
	{
		LOG_ERROR("发起订阅主题 '{}' 的请求失败，返回码 {}", topic, rc);
		delete static_cast<std::string*>(opts.context);
	}
	else
	{
		LOG_DEBUG("已发送订阅主题 '{}' 的请求。", topic);
	}
}

void plane::services::mqtt::MQTTService::shutdown(void) const noexcept
{
	if (impl_)
	{
		stopBackgroundThreads();
	}

	if (!impl_ || !impl_->client)
	{
		LOG_INFO("MQTTService shutdown：客户端不存在或已被销毁。");
		return;
	}

	if (MQTTAsync_isConnected(impl_->client))
	{
		LOG_INFO("MQTTService shutdown：客户端已连接，正在发送断开连接请求...");
		MQTTAsync_disconnectOptions opts = MQTTAsync_disconnectOptions_initializer;
		opts.timeout					 = 1000;
		if (int rc { MQTTAsync_disconnect(impl_->client, &opts) }; rc != MQTTASYNC_SUCCESS)
		{
			LOG_ERROR("MQTTService shutdown：断开连接调用失败，错误码 {}", rc);
		}
		else
		{
			LOG_INFO("MQTTService shutdown：断开连接调用成功。");
			std::this_thread::sleep_for(std::chrono::milliseconds(200));
		}
	}
	else
	{
		LOG_INFO("MQTTService shutdown：客户端未连接，跳过断开步骤。");
	}

	LOG_INFO("正在销毁 MQTT 客户端实例...");
	MQTTAsync_destroy(&impl_->client);
	impl_->client = nullptr;
}

void plane::services::mqtt::MQTTService::setConnected(bool status) noexcept
{
	connected_.store(status, std::memory_order_release);
}

bool plane::services::mqtt::MQTTService::isConnected(void) const noexcept
{
	return connected_.load(std::memory_order_acquire);
}

void plane::services::mqtt::MQTTService::heartbeatLoop(void) noexcept
{
	LOG_INFO("心跳线程已启动。");
	while (impl_->run_heartbeat_.load(std::memory_order_acquire))
	{
		if (this->isConnected())
		{
			LOG_DEBUG("发送 MQTT 心跳状态...");
			std::string online_json =
				fmt::format("{{\"status\":\"online\", \"timestamp\":{}}}",
							std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());

			this->publish(plane::services::mqtt::TOPIC_FIXED_INFO, online_json);

			// 未来可以在这里添加其他需要定时发送的消息
			// 例如：
			// std::string sensor_data = getSensorData();
			// this->publish("topic/sensor", sensor_data);
		}

		for (size_t i { 0 }; i < 100 && impl_->run_heartbeat_.load(std::memory_order_acquire); ++i)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
	}
	LOG_INFO("心跳线程已停止。");
}

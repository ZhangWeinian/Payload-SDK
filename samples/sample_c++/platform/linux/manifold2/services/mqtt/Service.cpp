#include "services/mqtt/Service.h"

#include "config/ConfigManager.h"
#include "services/mqtt/Handler/MessageHandler.h"
#include "services/mqtt/Topics.h"
#include "utils/JsonConverter/BuildAndParse.h"
#include "utils/Logger/Logger.h"

#include "fmt/format.h"

#include <chrono>
#include <cstring>
#include <thread>

namespace plane::services
{
	class MQTTPropertiesRAII
	{
	public:
		MQTTPropertiesRAII(void) noexcept
		{
			props_ = MQTTProperties_initializer;
		}

		~MQTTPropertiesRAII(void) noexcept
		{
			MQTTProperties_free(&props_);
		}

		MQTTProperties* get(void) noexcept
		{
			return &props_;
		}

		operator MQTTProperties*(void) noexcept
		{
			return &props_;
		}

	private:
		MQTTProperties props_ {};
	};

	static void onSubscribeSuccess(void* context, MQTTAsync_successData* response) noexcept
	{
		std::string* topic { static_cast<std::string*>(context) };
		LOG_DEBUG("成功订阅主题: '{}'", *topic);
		delete topic;
	}

	static void onSubscribeFailure(void* context, MQTTAsync_failureData* response) noexcept
	{
		std::string* topic { static_cast<std::string*>(context) };

		if (response)
		{
			LOG_ERROR("订阅主题 '{}' 失败，错误码: {}, 原因: {}", *topic, response->code, response->message ? response->message : "未知错误");
		}
		else
		{
			LOG_ERROR("订阅主题 '{}' 失败，无详细错误信息", *topic);
		}

		delete topic;
	}

	void onConnectSuccess(void* context, MQTTAsync_successData* response) noexcept
	{
		MQTTService* service { static_cast<MQTTService*>(context) };
		service->setConnected(true);
		LOG_INFO("MQTT 连接成功!（内部状态更新为已连接）");

		service->subscribe(plane::services::TOPIC_MISSION_CONTROL);
		service->subscribe(plane::services::TOPIC_COMMAND_CONTROL);
		service->subscribe(plane::services::TOPIC_PAYLOAD_CONTROL);
		service->subscribe(plane::services::TOPIC_ROCKER_CONTROL);
		service->subscribe(plane::services::TOPIC_VELOCITY_CONTROL);
	}

	void onConnectFailure(void* context, MQTTAsync_failureData* response) noexcept
	{
		MQTTService* service { static_cast<MQTTService*>(context) };
		service->setConnected(false);
		LOG_ERROR("MQTT 连接失败，错误码: {}, 消息: {}", response ? response->code : -1, response ? response->message : "未知错误");
	}

	static void onPublishSuccess(void* context, MQTTAsync_successData* response) noexcept
	{
		MQTTService* service { static_cast<MQTTService*>(context) };
		LOG_DEBUG("消息发布成功");
	}

	static void onPublishFailure(void* context, MQTTAsync_failureData* response) noexcept
	{
		MQTTService* service { static_cast<MQTTService*>(context) };
		LOG_ERROR("消息发布失败，错误码: {}, 原因: {}", response ? response->code : -1, response ? response->message : "未知错误");
	}

	static int messageArrived(void* context, char* topicName, int topicLen, MQTTAsync_message* message) noexcept
	{
		std::string topic(topicName, topicLen > 0 ? topicLen : strlen(topicName));
		std::string rawPayload(static_cast<char*>(message->payload), message->payloadlen);
		plane::utils::JsonConverter::parseAndRouteMessage(topic, rawPayload);
		MQTTAsync_freeMessage(&message);
		MQTTAsync_free(topicName);
		return 1;
	}

	void connectionLost(void* context, char* cause) noexcept
	{
		plane::services::MQTTService* service { static_cast<plane::services::MQTTService*>(context) };
		auto&						  impl = service->getImpl();
		if (!impl.manualDisconnect)
		{
			impl.reconnectAttempts++;
			impl.lastDisconnectTime = std::chrono::steady_clock::now();
			LOG_WARN("MQTT 连接已断开，原因: {}. 重连尝试次数: {}. Paho 库将自动重新连接。", cause ? cause : "未知", impl.reconnectAttempts);
		}
		else
		{
			LOG_INFO("MQTT 连接已手动断开");
			impl.manualDisconnect = false;
		}

		service->setConnected(false);
	}

	static void deliveryComplete(void* context, MQTTAsync_token token)
	{
		LOG_DEBUG("MQTT 消息发送完成，令牌: {}", token);
	}

	MQTTService::~MQTTService(void) noexcept
	{
		stop();
	}

	MQTTService& MQTTService::getInstance(void) noexcept
	{
		static MQTTService instance {};
		return instance;
	}

	void MQTTService::publish(const std::string& topic, const std::string& payload) noexcept
	{
		std::lock_guard<std::mutex> lock(mutex_);

		if (!isConnected() || !impl_->client)
		{
			LOG_WARN("MQTT 未连接，发布请求被忽略。");
			return;
		}

		MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
		opts.context				   = this;
		opts.onSuccess				   = onPublishSuccess;
		opts.onFailure				   = onPublishFailure;

		if (int rc { MQTTAsync_send(impl_->client, topic.c_str(), payload.length(), payload.c_str(), 1, 0, &opts) }; rc != MQTTASYNC_SUCCESS)
		{
			LOG_ERROR("向主题 '{}' 发布消息失败，返回码 {}", topic, rc);
		}
	}

	void MQTTService::subscribe(const std::string& topic) noexcept
	{
		std::lock_guard<std::mutex> lock(mutex_);

		if (!isConnected() || !impl_->client)
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

	bool MQTTService::start(const std::string& serverURI, const std::string& clientId) noexcept
	{
		std::lock_guard<std::mutex> lock(mutex_);

		if (isConnected())
		{
			LOG_WARN("MQTT 服务已经启动，忽略重复启动请求");
			return true;
		}

		std::string url { serverURI };
		std::string cid { clientId };

		if (url.empty() || cid.empty())
		{
			url = config::ConfigManager::getInstance().getMqttUrl();
			cid = config::ConfigManager::getInstance().getMqttClientId();
			LOG_DEBUG("使用配置文件中的 MQTT 设置启动服务。服务器=[{}]，客户端ID=[{}]", url, cid);
		}
		else
		{
			LOG_DEBUG("使用提供的 MQTT 设置启动服务。服务器=[{}]，客户端ID=[{}]", url, cid);
		}

		if (impl_->client)
		{
			LOG_DEBUG("清理现有的 MQTT 客户端实例");
			stop();
		}

		if (!initializeClient(url, cid))
		{
			LOG_ERROR("MQTT 客户端初始化失败");
			return false;
		}

		if (!connectToBroker())
		{
			LOG_ERROR("MQTT 连接失败");
			if (impl_->client)
			{
				MQTTAsync_destroy(&impl_->client);
				impl_->client = nullptr;
			}
			return false;
		}

		LOG_DEBUG("MQTT 服务启动成功，等待连接建立...");
		return true;
	}

	bool MQTTService::initializeClient(const std::string& serverURI, const std::string& clientId) noexcept
	{
		impl_->serverURI = serverURI;
		impl_->clientId	 = clientId;

		LOG_INFO("初始化 MQTT 客户端: 服务器={}, 客户端ID={}", impl_->serverURI, impl_->clientId);

		if (int rc { MQTTAsync_create(&impl_->client, impl_->serverURI.c_str(), impl_->clientId.c_str(), MQTTCLIENT_PERSISTENCE_NONE, nullptr) };
			rc != MQTTASYNC_SUCCESS)
		{
			LOG_ERROR("MQTTAsync_create 失败，返回码 {}", rc);
			impl_->client = nullptr;
			return false;
		}

		if (int rc { MQTTAsync_setCallbacks(impl_->client, this, connectionLost, messageArrived, deliveryComplete) }; rc != MQTTASYNC_SUCCESS)
		{
			LOG_ERROR("设置 MQTT 回调失败，返回码 {}", rc);
			MQTTAsync_destroy(&impl_->client);
			impl_->client = nullptr;
			return false;
		}

		LOG_DEBUG("MQTT 客户端创建成功，准备连接");
		return true;
	}

	bool MQTTService::connectToBroker(void) noexcept
	{
		MQTTAsync_connectOptions conn_opts = MQTTAsync_connectOptions_initializer;
		strncpy(conn_opts.struct_id, "MQTC", 4);
		conn_opts.keepAliveInterval	 = 30;				 // 30 秒心跳间隔
		conn_opts.cleansession		 = 1;				 // 保持会话状态，允许离线消息
		conn_opts.connectTimeout	 = 10;				 // 10秒连接超时
		conn_opts.retryInterval		 = 0;				 // 禁用会话内重试
		conn_opts.automaticReconnect = 1;				 // 启用自动重连
		conn_opts.minRetryInterval	 = 1;				 // 最小重试间隔 1 秒
		conn_opts.maxRetryInterval	 = 60;				 // 最大重试间隔 60 秒
		conn_opts.maxInflight		 = 1000;			 // 允许最多 1000 个未确认消息
		conn_opts.onSuccess			 = onConnectSuccess; // 连接成功回调
		conn_opts.onFailure			 = onConnectFailure; // 连接失败回调
		conn_opts.context			 = this;			 // 回调上下文

		// conn_opts.cleansession		 = 1;

		LOG_DEBUG("使用 MQTT v5 协议连接服务器: {}", impl_->serverURI);

		if (int rc { MQTTAsync_connect(impl_->client, &conn_opts) }; rc != MQTTASYNC_SUCCESS)
		{
			LOG_ERROR("启动 MQTT v5 连接失败，返回码: {}", rc);
			return false;
		}

		LOG_DEBUG("MQTT v5 连接请求已发送，等待连接结果...");
		return true;
	}

	void MQTTService::stop(void) noexcept
	{
		std::lock_guard<std::mutex> lock(mutex_);

		impl_->manualDisconnect = true;
		setConnected(false);

		if (!impl_ || !impl_->client)
		{
			LOG_INFO("MQTTService 停止：客户端不存在或已被销毁");
			return;
		}

		if (int rc { MQTTAsync_isConnected(impl_->client) }; rc == 1)
		{
			LOG_DEBUG("MQTTService 停止：客户端已连接，正在发送断开连接请求...");
			MQTTAsync_disconnectOptions opts = MQTTAsync_disconnectOptions_initializer;
			opts.timeout					 = 1000;

			if (int rc { MQTTAsync_disconnect(impl_->client, &opts) }; rc != MQTTASYNC_SUCCESS)
			{
				LOG_ERROR("MQTTService 停止：断开连接调用失败，错误码 {}", rc);
			}
			else
			{
				LOG_DEBUG("MQTTService 停止：断开连接请求已发送");
				std::this_thread::sleep_for(std::chrono::milliseconds(200));
			}
		}
		else
		{
			LOG_DEBUG("MQTTService 停止：客户端未连接，跳过断开步骤");
		}

		LOG_DEBUG("正在销毁 MQTT 客户端实例...");
		MQTTAsync_destroy(&impl_->client);
		impl_->client = nullptr;

		LOG_INFO("MQTT 服务已停止");
	}

	void MQTTService::restart(const std::string& serverURI, const std::string& clientId) noexcept
	{
		std::lock_guard<std::mutex> lock(mutex_);
		LOG_INFO("重新启动 MQTT 服务...");
		stop();
		std::this_thread::sleep_for(std::chrono::milliseconds(300));
		start(serverURI, clientId);
	}

	void MQTTService::setConnected(bool status) noexcept
	{
		connected_.store(status, std::memory_order_release);
	}

	bool MQTTService::isConnected(void) const noexcept
	{
		return connected_.load(std::memory_order_acquire);
	}
} // namespace plane::services

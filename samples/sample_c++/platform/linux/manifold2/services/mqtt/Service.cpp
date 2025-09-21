#include "services/mqtt/Service.h"

#include "config/ConfigManager.h"
#include "services/mqtt/Handler/MessageHandler.h"
#include "services/mqtt/Topics.h"
#include "utils/JsonConverter/BuildAndParse.h"
#include "utils/Logger/Logger.h"

#include "fmt/format.h"
#include "mqtt/async_client.h"

#include <chrono>
#include <thread>

using namespace mqtt;

namespace plane::services
{
	class MqttCallback: public callback,
						public iaction_listener
	{
	public:
		explicit MqttCallback(MQTTService* service): service_(service) {}

		void connected(const std::string& cause) override
		{
			service_->setConnected(true);
			LOG_INFO("MQTT 连接成功!（内部状态更新为已连接） cause: {}", cause);
			service_->subscribe(TOPIC_MISSION_CONTROL);
			service_->subscribe(TOPIC_COMMAND_CONTROL);
			service_->subscribe(TOPIC_PAYLOAD_CONTROL);
			service_->subscribe(TOPIC_ROCKER_CONTROL);
			service_->subscribe(TOPIC_VELOCITY_CONTROL);
		}

		void connection_lost(const std::string& cause) override
		{
			auto& impl { service_->getImpl() };
			if (!impl.manualDisconnect)
			{
				impl.reconnectAttempts++;
				impl.lastDisconnectTime = std::chrono::steady_clock::now();
				LOG_WARN("MQTT 连接已断开，原因: {}. 重连尝试次数: {}.", cause, impl.reconnectAttempts);
			}
			else
			{
				LOG_INFO("MQTT 连接已手动断开");
				impl.manualDisconnect = false;
			}
			service_->setConnected(false);
		}

		void message_arrived(mqtt::const_message_ptr msg) override
		{
			LOG_DEBUG("收到消息: topic={}, payload={}", msg->get_topic(), msg->to_string());
			plane::utils::JsonConverter::parseAndRouteMessage(msg->get_topic(), msg->to_string());
		}

		void delivery_complete(delivery_token_ptr token) override
		{
			LOG_DEBUG("MQTT 消息发送完成，令牌: {}", token ? token->get_message_id() : -1);
		}

		void on_failure(const token& tok) override
		{
			LOG_ERROR("MQTT 操作失败，token: {}", tok.get_message_id());
		}

		void on_success(const token& tok) override
		{
			LOG_DEBUG("MQTT 操作成功，token: {}", tok.get_message_id());
		}

	private:
		MQTTService* service_ {};
	};

	MQTTService::~MQTTService(void) noexcept
	{
		try
		{
			stop();
			LOG_DEBUG("[MQTTService::dtor] stop() 完成");
		}
		catch (const std::exception& ex)
		{
			LOG_ERROR("MQTTService 析构异常: {}", ex.what());
		}
		catch (...)
		{
			LOG_ERROR("MQTTService 析构发生未知异常: <non-std exception>");
		}
	}

	MQTTService& MQTTService::getInstance(void) noexcept
	{
		static MQTTService instance {};
		return instance;
	}

	bool MQTTService::publish(const std::string& topic, const std::string& payload) noexcept
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (!isConnected() || !impl_->client)
		{
			LOG_WARN("MQTT 未连接，发布请求被忽略。");
			return false;
		}

		try
		{
			auto msg { mqtt::make_message(topic, payload) };
			msg->set_qos(1);
			impl_->client->publish(msg);
			LOG_DEBUG("已向主题 '{}' 发送消息发布请求。", topic);
			return true;
		}
		catch (const mqtt::exception& ex)
		{
			LOG_ERROR("向主题 '{}' 发布消息失败: {}，client={}", topic, ex.what(), (void*)impl_->client.get());
			return false;
		}
		catch (const std::exception& ex)
		{
			LOG_ERROR("向主题 '{}' 发布消息发生未知异常: {}，client={}", topic, ex.what(), (void*)impl_->client.get());
			return false;
		}
		catch (...)
		{
			LOG_ERROR("向主题 '{}' 发布消息发生未知异常: <non-std exception>，client={}", topic, (void*)impl_->client.get());
			return false;
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

		try
		{
			impl_->client->subscribe(topic, 1);
			LOG_DEBUG("已发送订阅主题 '{}' 的请求。", topic);
		}
		catch (const mqtt::exception& ex)
		{
			LOG_ERROR("发起订阅主题 '{}' 的请求失败: {}", topic, ex.what());
		}
		catch (const std::exception& ex)
		{
			LOG_ERROR("发起订阅主题 '{}' 的请求发生未知异常: {}", topic, ex.what());
		}
		catch (...)
		{
			LOG_ERROR("发起订阅主题 '{}' 的请求发生未知异常: <non-std exception>", topic);
		}
	}

	bool MQTTService::start(const std::string& serverURI, const std::string& clientId) noexcept
	{
		std::lock_guard<std::mutex> lock(mutex_);

		if (isConnected())
		{
			LOG_DEBUG("MQTT 服务已经启动，忽略重复启动请求");
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
			LOG_DEBUG("[start] stop() 完成，client 已清理");
		}

		try
		{
			impl_->client	= std::make_unique<mqtt::async_client>(url, cid);
			impl_->callback = std::make_shared<MqttCallback>(this);
			impl_->client->set_callback(*impl_->callback);
			mqtt::connect_options connOpts;
			connOpts.set_keep_alive_interval(30);
			connOpts.set_clean_session(true);
			connOpts.set_automatic_reconnect(true);
			connOpts.set_mqtt_version(MQTTVERSION_5);
			impl_->client->connect(connOpts);
			LOG_DEBUG("MQTT 服务启动成功，等待连接建立...");
			return true;
		}
		catch (const mqtt::exception& ex)
		{
			LOG_ERROR("MQTT 客户端初始化或连接失败: {}", ex.what());
			impl_->client.reset();
			impl_->callback.reset();
			return false;
		}
		catch (const std::exception& ex)
		{
			LOG_ERROR("MQTT 客户端初始化或连接发生未知异常: {}", ex.what());
			impl_->client.reset();
			impl_->callback.reset();
			return false;
		}
		catch (...)
		{
			LOG_ERROR("MQTT 客户端初始化或连接发生未知异常: <non-std exception>");
			impl_->client.reset();
			impl_->callback.reset();
			return false;
		}
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

		try
		{
			if (impl_->client->is_connected())
			{
				LOG_DEBUG("MQTTService 停止：客户端已连接，正在发送断开连接请求...");
				impl_->client->disconnect()->wait();
				std::this_thread::sleep_for(std::chrono::milliseconds(200));
			}
			else
			{
				LOG_DEBUG("MQTTService 停止：客户端未连接，跳过断开步骤");
			}
			LOG_DEBUG("正在销毁 MQTT 客户端实例...");
			impl_->client.reset();
			impl_->callback.reset();
			LOG_INFO("MQTT 服务已停止");
		}
		catch (const mqtt::exception& ex)
		{
			LOG_ERROR("MQTTService 停止异常: {}", ex.what());
		}
		catch (const std::exception& ex)
		{
			LOG_ERROR("MQTTService 停止发生未知异常: {}", ex.what());
		}
		catch (...)
		{
			LOG_ERROR("MQTTService 停止发生未知异常: <non-std exception>");
		}
	}

	void MQTTService::restart(const std::string& serverURI, const std::string& clientId) noexcept
	{
		std::lock_guard<std::mutex> lock(mutex_);
		LOG_INFO("重新启动 MQTT 服务...");
		stop();
		std::this_thread::sleep_for(std::chrono::milliseconds(300));
		if (!start(serverURI, clientId))
		{
			LOG_ERROR("MQTT restart 启动失败！");
		}
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

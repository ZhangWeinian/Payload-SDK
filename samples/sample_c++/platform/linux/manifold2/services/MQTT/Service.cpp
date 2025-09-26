// manifold2/services/MQTT/Service.cpp

#include "services/MQTT/Service.h"

#include <chrono>
#include <thread>

#include <fmt/format.h>
#include <mqtt/async_client.h>

#include "config/ConfigManager.h"
#include "services/MQTT/Handler/MessageHandler.h"
#include "services/MQTT/Topics.h"
#include "utils/JsonConverter/BuildAndParse.h"
#include "utils/Logger.h"

namespace plane::services
{
	class MqttCallback: public _MQTT callback,
						public _MQTT iaction_listener
	{
	public:
		explicit MqttCallback(MQTTService* service): service_(service) {}

		void connected(const _STD string& cause) override
		{
			service_->setConnected(true);
			LOG_INFO("MQTT 连接成功！");
			service_->subscribe(plane::services::TOPIC_MISSION_CONTROL);
			service_->subscribe(plane::services::TOPIC_COMMAND_CONTROL);
			service_->subscribe(plane::services::TOPIC_PAYLOAD_CONTROL);
			service_->subscribe(plane::services::TOPIC_ROCKER_CONTROL);
			service_->subscribe(plane::services::TOPIC_VELOCITY_CONTROL);
		}

		void connection_lost(const _STD string& cause) override
		{
			auto& impl { service_->getImpl() };
			if (!impl.manualDisconnect)
			{
				impl.reconnectAttempts++;
				impl.lastDisconnectTime = _STD_CHRONO steady_clock::now();
				LOG_WARN("MQTT 连接已断开, 原因: {}. 重连尝试次数: {}.", cause, impl.reconnectAttempts);
			}
			else
			{
				LOG_INFO("MQTT 连接已手动断开");
				impl.manualDisconnect = false;
			}
			service_->setConnected(false);
		}

		void message_arrived(_MQTT const_message_ptr msg) override
		{
			LOG_DEBUG("收到消息: topic={}, payload={}", msg->get_topic(), msg->to_string());
			plane::utils::JsonConverter::parseAndRouteMessage(msg->get_topic(), msg->to_string());
		}

		void delivery_complete(_MQTT delivery_token_ptr token) override
		{
			LOG_DEBUG("MQTT 消息发送完成, 令牌: {}", token ? token->get_message_id() : -1);
		}

		void on_failure(const _MQTT token& tok) override
		{
			LOG_ERROR("MQTT 操作失败, token: {}", tok.get_message_id());
		}

		void on_success(const _MQTT token& tok) override
		{
			LOG_DEBUG("MQTT 操作成功, token: {}", tok.get_message_id());
		}

	private:
		MQTTService* service_ {};
	};

	MQTTService::~MQTTService(void) noexcept
	{
		try
		{
			if (!impl_ || !impl_->client)
			{
				return;
			}

			stop();
			LOG_DEBUG("[MQTTService::dtor] stop() 完成");
		}
		catch (const _STD exception& ex)
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

	bool MQTTService::publish(_STD string_view topic, _STD string_view payload) noexcept
	{
		_STD lock_guard<_STD mutex> lock(mutex_);
		if (!isConnected() || !impl_->client)
		{
			LOG_WARN("MQTT 未连接, 发布请求被忽略。");
			return false;
		}

		try
		{
			auto msg { _MQTT make_message(topic.data(), payload.data()) };
			msg->set_qos(1);
			impl_->client->publish(msg);
			LOG_DEBUG("已向主题 '{}' 发送消息发布请求。", topic);
			return true;
		}
		catch (const _MQTT exception& ex)
		{
			LOG_ERROR("向主题 '{}' 发布消息失败: {}, client={}", topic, ex.what(), (void*)impl_->client.get());
			return false;
		}
		catch (const _STD exception& ex)
		{
			LOG_ERROR("向主题 '{}' 发布消息发生未知异常: {}, client={}", topic, ex.what(), (void*)impl_->client.get());
			return false;
		}
		catch (...)
		{
			LOG_ERROR("向主题 '{}' 发布消息发生未知异常: <non-std exception>, client={}", topic, (void*)impl_->client.get());
			return false;
		}
	}

	void MQTTService::subscribe(_STD string_view topic) noexcept
	{
		_STD lock_guard<_STD mutex> lock(mutex_);
		if (!isConnected() || !impl_->client)
		{
			LOG_WARN("MQTT 未连接, 对主题 '{}' 的订阅请求被忽略。", topic);
			return;
		}

		try
		{
			impl_->client->subscribe(topic.data(), 1);
			LOG_DEBUG("已发送订阅主题 '{}' 的请求。", topic);
		}
		catch (const _MQTT exception& ex)
		{
			LOG_ERROR("发起订阅主题 '{}' 的请求失败: {}", topic, ex.what());
		}
		catch (const _STD exception& ex)
		{
			LOG_ERROR("发起订阅主题 '{}' 的请求发生未知异常: {}", topic, ex.what());
		}
		catch (...)
		{
			LOG_ERROR("发起订阅主题 '{}' 的请求发生未知异常: <non-std exception>", topic);
		}
	}

	bool MQTTService::start(_STD string_view serverURI, _STD string_view clientId) noexcept
	{
		_STD lock_guard<_STD mutex> lock(mutex_);

		if (isConnected())
		{
			LOG_DEBUG("MQTT 服务已经启动, 忽略重复启动请求");
			return true;
		}

		_STD string url { serverURI };
		_STD string cid { clientId };

		if (url.empty() || cid.empty())
		{
			url = config::ConfigManager::getInstance().getMqttUrl();
			cid = config::ConfigManager::getInstance().getMqttClientId();
			LOG_INFO("使用配置文件中的 MQTT 设置启动服务。服务器=[{}], 客户端ID=[{}]", url, cid);
		}
		else
		{
			LOG_INFO("使用提供的 MQTT 设置启动服务。服务器=[{}], 客户端ID=[{}]", url, cid);
		}

		if (impl_->client)
		{
			LOG_DEBUG("清理现有的 MQTT 客户端实例");
			stop();
			LOG_DEBUG("[start] stop() 完成, client 已清理");
		}

		try
		{
			impl_->client	= _STD	 make_unique<_MQTT async_client>(url, cid);
			impl_->callback = _STD make_shared<MqttCallback>(this);
			impl_->client->set_callback(*impl_->callback);
			_MQTT connect_options connOpts;
			connOpts.set_keep_alive_interval(30);
			connOpts.set_clean_session(true);
			connOpts.set_automatic_reconnect(true);
			connOpts.set_mqtt_version(MQTTVERSION_5);
			impl_->client->connect(connOpts);
			LOG_DEBUG("MQTT 服务启动成功, 等待连接建立...");
			return true;
		}
		catch (const _MQTT exception& ex)
		{
			LOG_ERROR("MQTT 客户端初始化或连接失败: {}", ex.what());
			impl_->client.reset();
			impl_->callback.reset();
			return false;
		}
		catch (const _STD exception& ex)
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
		_STD lock_guard<_STD mutex> lock(mutex_);

		impl_->manualDisconnect = true;
		setConnected(false);

		if (!impl_ || !impl_->client)
		{
			LOG_DEBUG("MQTTService 停止：客户端不存在或已被销毁");
			return;
		}

		try
		{
			if (impl_->client->is_connected())
			{
				LOG_DEBUG("MQTTService 停止：客户端已连接, 正在发送断开连接请求...");
				impl_->client->disconnect()->wait();
				_STD this_thread::sleep_for(_STD_CHRONO milliseconds(200));
			}
			else
			{
				LOG_DEBUG("MQTTService 停止：客户端未连接, 跳过断开步骤");
			}
			LOG_DEBUG("正在销毁 MQTT 客户端实例...");
			impl_->client.reset();
			impl_->callback.reset();
			LOG_INFO("MQTT 服务已停止");
		}
		catch (const _MQTT exception& ex)
		{
			LOG_ERROR("MQTTService 停止异常: {}", ex.what());
		}
		catch (const _STD exception& ex)
		{
			LOG_ERROR("MQTTService 停止发生未知异常: {}", ex.what());
		}
		catch (...)
		{
			LOG_ERROR("MQTTService 停止发生未知异常: <non-std exception>");
		}
	}

	void MQTTService::restart(_STD string_view serverURI, _STD string_view clientId) noexcept
	{
		_STD lock_guard<_STD mutex> lock(mutex_);
		LOG_INFO("重新启动 MQTT 服务...");
		stop();
		_STD this_thread::sleep_for(_STD_CHRONO milliseconds(300));
		if (!start(serverURI, clientId))
		{
			LOG_ERROR("MQTT restart 启动失败！");
		}
	}

	void MQTTService::setConnected(bool status) noexcept
	{
		connected_.store(status, _STD memory_order_release);
	}

	bool MQTTService::isConnected(void) const noexcept
	{
		return connected_.load(_STD memory_order_acquire);
	}
} // namespace plane::services

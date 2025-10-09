// raspberry_pi/services/MQTT/Service.cpp

#include "Service.h"

#include "config/ConfigManager.h"
#include "services/MQTT/Handler/MessageHandler.h"
#include "services/MQTT/Topics.h"
#include "utils/JsonConverter/BuildAndParse.h"
#include "utils/Logger.h"

#include <fmt/format.h>
#include <mqtt/async_client.h>

#include <chrono>
#include <thread>

namespace plane::services
{
	class MqttCallback: public _MQTT callback,
						public _MQTT iaction_listener
	{
	public:
		explicit MqttCallback(MQTTService* service): service_(service) {}

		void connected(const _STD string& cause) override
		{
			this->service_->setConnected(true);
			LOG_INFO("MQTT 连接成功！");
			this->service_->subscribe(plane::services::TOPIC_MISSION_CONTROL);
			this->service_->subscribe(plane::services::TOPIC_COMMAND_CONTROL);
			this->service_->subscribe(plane::services::TOPIC_PAYLOAD_CONTROL);
			this->service_->subscribe(plane::services::TOPIC_ROCKER_CONTROL);
			this->service_->subscribe(plane::services::TOPIC_VELOCITY_CONTROL);
			LOG_INFO("MQTT 初始主题订阅成功！");
		}

		void connection_lost(const _STD string& cause) override
		{
			auto& impl { this->service_->getImpl() };
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
			this->service_->setConnected(false);
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
			this->stop();
		}
		catch (const _STD exception& ex)
		{
			LOG_ERROR("MQTT 服务析构异常: {}", ex.what());
		}
		catch (...)
		{
			LOG_ERROR("MQTT 服务析构发生未知异常: <non-std exception>");
		}
	}

	bool MQTTService::start(void) noexcept
	{
		_STD lock_guard<_STD mutex> lock(this->mutex_);

		if (this->isConnected())
		{
			LOG_DEBUG("MQTT 服务已经启动, 忽略重复启动请求");
			return true;
		}

		_STD string url { plane::config::ConfigManager::getInstance().getMqttUrl() };
		_STD string cid { plane::config::ConfigManager::getInstance().getMqttClientId() };
		LOG_INFO("使用配置文件中的 MQTT 设置启动服务。服务器=[{}], 客户端ID=[{}]", url, cid);

		if (this->impl_->client)
		{
			LOG_DEBUG("清理现有的 MQTT 客户端实例");
			this->stop();
			LOG_DEBUG("stop() 完成, client 已清理");
		}

		try
		{
			this->impl_->client	  = _STD   make_unique<_MQTT async_client>(url, cid);
			this->impl_->callback = _STD make_shared<MqttCallback>(this);
			this->impl_->client->set_callback(*(this->impl_)->callback);

			_MQTT connect_options connOpts {};
			connOpts.set_keep_alive_interval(30);
			connOpts.set_clean_session(true);
			connOpts.set_automatic_reconnect(true);
			connOpts.set_mqtt_version(MQTTVERSION_5);

			this->impl_->client->connect(connOpts);
			LOG_DEBUG("MQTT 服务启动成功, 等待连接建立...");

			this->impl_->runSender	  = true;
			this->impl_->senderThread = _STD thread(&MQTTService::senderLoop, this);
			LOG_DEBUG("MQTT 异步发送线程已启动。");

			return true;
		}
		catch (const _MQTT exception& ex)
		{
			LOG_ERROR("MQTT 客户端初始化或连接失败: {}", ex.what());
			this->impl_->client.reset();
			this->impl_->callback.reset();
			return false;
		}
		catch (const _STD exception& ex)
		{
			LOG_ERROR("MQTT 客户端初始化或连接发生未知异常: {}", ex.what());
			this->impl_->client.reset();
			this->impl_->callback.reset();
			return false;
		}
		catch (...)
		{
			LOG_ERROR("MQTT 客户端初始化或连接发生未知异常: <non-std exception>");
			this->impl_->client.reset();
			this->impl_->callback.reset();
			return false;
		}
	}

	void MQTTService::stop(void) noexcept
	{
		if (!this->stopped_.exchange(true))
		{
			return;
		}

		if (this->impl_->runSender.exchange(false))
		{
			this->impl_->dequeCv.notify_one();
			if (this->impl_->senderThread.joinable())
			{
				this->impl_->senderThread.join();
				LOG_DEBUG("MQTT 异步发送线程已停止。");
			}
		}

		_STD lock_guard<_STD mutex> lock(this->mutex_);

		this->impl_->manualDisconnect = true;
		this->setConnected(false);

		if (!this->impl_ || !this->impl_->client)
		{
			LOG_DEBUG("MQTTService 停止：客户端不存在或已被销毁");
			return;
		}

		try
		{
			if (this->impl_->client->is_connected())
			{
				LOG_DEBUG("MQTTService 停止：客户端已连接, 正在发送断开连接请求...");
				this->impl_->client->disconnect()->wait();
				_STD this_thread::sleep_for(_STD_CHRONO milliseconds(200));
			}
			else
			{
				LOG_DEBUG("MQTTService 停止：客户端未连接, 跳过断开步骤");
			}

			this->impl_->client.reset();
			this->impl_->callback.reset();
			LOG_INFO("MQTT 服务已停止。");
		}
		catch (const _MQTT exception& ex)
		{
			LOG_ERROR("MQTTService 停止异常（来自 MQTT）: {}", ex.what());
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

	void MQTTService::restart(void) noexcept
	{
		_STD lock_guard<_STD mutex> lock(this->mutex_);
		LOG_INFO("重新启动 MQTT 服务...");
		this->stop();
		_STD this_thread::sleep_for(_STD_CHRONO milliseconds(300));
		if (!this->start())
		{
			LOG_ERROR("MQTT restart 启动失败！");
		}
	}

	MQTTService& MQTTService::getInstance(void) noexcept
	{
		static MQTTService instance {};
		return instance;
	}

	bool MQTTService::publish(_STD string_view topic, _STD string_view payload) noexcept
	{
		if (!this->impl_->runSender)
		{
			LOG_WARN("MQTT 发送服务未运行, 消息被丢弃。");
			return false;
		}

		{
			_STD lock_guard<_STD mutex> lock(this->impl_->dequeMutex);

			if (this->impl_->messageDeque.size() >= this->MAX_DEQUE_SIZE)
			{
				this->impl_->messageDeque.pop_front();
				if (const auto now { _STD_CHRONO steady_clock::now() };
					!this->impl_->isDroppingMessages || (now - this->impl_->lastDropLogTime > this->LOG_THROTTLE_INTERVAL))
				{
					LOG_WARN("MQTT 消息队列已满, 正在丢弃最旧的消息以保证数据新鲜度。此警告将在 {} 秒内抑制。",
							 this->LOG_THROTTLE_INTERVAL.count());
					this->impl_->isDroppingMessages = true;
					this->impl_->lastDropLogTime	= now;
				}
				else
				{
					LOG_DEBUG("MQTT 消息队列已满, 丢弃最旧消息 (日志已抑制)。");
				}
			}
			else
			{
				this->impl_->isDroppingMessages = false;
			}

			this->impl_->messageDeque.emplace_back(topic, payload);
		}

		this->impl_->dequeCv.notify_one();
		return true;
	}

	void MQTTService::subscribe(_STD string_view topic) noexcept
	{
		_STD lock_guard<_STD mutex> lock(this->mutex_);
		if (!this->isConnected() || !this->impl_->client)
		{
			LOG_WARN("MQTT 未连接, 对主题 '{}' 的订阅请求被忽略。", topic);
			return;
		}

		try
		{
			this->impl_->client->subscribe(topic.data(), 1);
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

	void MQTTService::setConnected(bool status) noexcept
	{
		this->connected_.store(status, _STD memory_order_release);
	}

	bool MQTTService::isConnected(void) const noexcept
	{
		return this->connected_.load(_STD memory_order_acquire);
	}

	void MQTTService::senderLoop() noexcept
	{
		LOG_DEBUG("MQTT 发送者线程循环开始。");

		while (this->impl_->runSender)
		{
			_STD pair<_STD string, _STD string> message {};

			{
				_STD unique_lock<_STD mutex> lock(this->impl_->dequeMutex);
				this->impl_->dequeCv.wait(lock,
										  [this]
										  {
											  return !this->impl_->messageDeque.empty() || !this->impl_->runSender;
										  });

				if (!this->impl_->runSender && this->impl_->messageDeque.empty())
				{
					break;
				}

				if (this->impl_->messageDeque.empty())
				{
					continue;
				}

				message = _STD move(this->impl_->messageDeque.front());
				this->impl_->messageDeque.pop_front();
			}

			if (!this->isConnected() || !this->impl_ || !this->impl_->client)
			{
				LOG_WARN("MQTT 未连接, 队列中的一条消息被丢弃。");
				continue;
			}

			try
			{
				auto msg { _MQTT make_message(message.first, message.second) };
				msg->set_qos(1);
				this->impl_->client->publish(msg);
			}
			catch (const _MQTT exception& ex)
			{
				LOG_ERROR("发送者线程发布消息到主题 '{}' 失败，消息被丢弃: {}", message.first, ex.what());
			}
			catch (...)
			{
				LOG_ERROR("发送者线程发布消息 '{}' 时发生未知异常，消息被丢弃。", message.first);
			}
		}

		LOG_INFO("MQTT 发送者线程循环已结束。");
	}
} // namespace plane::services

// cy_psdk/manager/mqtt/service/MQTTv5Service.cpp

#include "manager/mqtt/service/MQTTv5Service.h"

#include "config/ConfigManager.h"
#include "manager/mqtt/handler/MessageHandler.h"
#include "manager/mqtt/MQTTTopics.h"
#include "utils/json_converter/BuildAndParse.h"
#include "utils/log_util/Logger.h"

#include <fmt/format.h>
#include <mqtt/async_client.h>

#include <chrono>
#include <thread>

namespace plane::manager
{
	class MqttCallback final: public _MQTT callback,
							  public _MQTT iaction_listener
	{
	public:
		explicit MqttCallback(plane::manager::MQTTv5Service* service): service_(service) {}

		void connected(const _STD string& cause) override
		{
			this->service_->setConnected(true);
			LOG_INFO("MQTT 连接成功！");
			this->service_->subscribe(plane::manager::TOPIC_MISSION_CONTROL);
			this->service_->subscribe(plane::manager::TOPIC_COMMAND_CONTROL);
			this->service_->subscribe(plane::manager::TOPIC_PAYLOAD_CONTROL);
			this->service_->subscribe(plane::manager::TOPIC_ROCKER_CONTROL);
			this->service_->subscribe(plane::manager::TOPIC_VELOCITY_CONTROL);
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

			// 给 paho 内建自动重连留出窗口; 维护线程在窗口超时后兜底重建
			{
				_STD lock_guard<_STD mutex>		   lock { impl.clientMutex };
				impl.lastAttemptTime = _STD_CHRONO steady_clock::now();
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
			LOG_TRACE("MQTT 消息发送完成, 令牌: {}", token ? token->get_message_id() : -1);
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
		MQTTv5Service* service_ {};
	};

	MQTTv5Service::~MQTTv5Service(void) noexcept
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

	bool MQTTv5Service::start(void) noexcept
	{
		if (bool expected { false }; !this->running_.compare_exchange_strong(expected, true))
		{
			LOG_DEBUG("MQTT 服务已经启动, 忽略重复启动请求");
			return true;
		}

		if (this->impl_->client)
		{
			LOG_WARN("检测到残留的 MQTT 客户端，将先执行清理");
			try
			{
				if (this->impl_->client->is_connected())
				{
					this->impl_->client->disconnect()->wait();
				}
			}
			catch (const _MQTT exception& ex)
			{
				LOG_ERROR("残留的 MQTT 客户端断开连接时出现异常: {}", ex.what());
			}
			catch (const _STD exception& ex)
			{
				LOG_ERROR("残留的 MQTT 客户端断开连接时出现未知异常: {}", ex.what());
			}
			catch (...)
			{
				LOG_ERROR("残留的 MQTT 客户端断开连接时出现未知异常: <non-std exception>");
			}

			this->impl_.reset(new Impl());
		}

		_STD string url { plane::config::ConfigManager::getInstance().getMqttUrl() };
		{
			// Catalog 服务发现结果优先于静态配置
			_STD lock_guard<_STD mutex> lock { this->mutex_ };
			if (!this->broker_url_override_.empty())
			{
				url = this->broker_url_override_;
			}
		}
		if (url.empty())
		{
			LOG_WARN("MQTT broker 地址为空, 进入等待模式; 自检线程将周期复查, 地址就绪后自动连接");
		}
		_STD string cid { plane::config::ConfigManager::getInstance().getMqttClientId() };
		LOG_INFO("MQTT 服务配置: 服务器={}, 客户端ID={}", url, cid);

		try
		{
			// 订阅系统事件: Catalog 服务发现解析到中心 broker 后广播地址, 作为快路径立即唤醒自检
			auto&							   dispatcher { plane::manager::EventManager::getInstance().getSystemDispatcher() };
			this->system_event_remover_ = _STD make_unique<_EVENTPP ScopedRemover<plane::manager::EventManager::SystemDispatcher>>(dispatcher);
			this->system_event_remover_->appendListener(
				plane::manager::EventManager::SystemEvent::MqttBrokerUpdated,
				[this](const plane::manager::EventManager::SystemEventData& data)
				{
					if (const auto* new_url { _STD get_if<_STD string>(&data) })
					{
						this->setBrokerUrlOverride(*new_url); // 内部会唤醒维护线程
					}
				}
			);

			// 发送线程: 消息队列出队发布
			this->impl_->runSender	  = true;
			this->impl_->senderThread = _STD thread(&MQTTv5Service::senderLoop, this);

			// 维护线程: 周期自检 broker 连接 (地址等待/变化重连/断线兜底)
			this->impl_->runMaintain	= true;
			this->impl_->maintainThread = _STD thread(&MQTTv5Service::maintainLoop, this);

			// 立即唤醒一次: 有地址时无需等待首个自检周期
			this->impl_->maintainCv.notify_all();

			LOG_INFO("MQTT 服务已启动 (自治运行: 发送线程 + {}s 自检维护线程)", kMaintainInterval.count());
			return true;
		}
		catch (const _MQTT exception& ex)
		{
			LOG_ERROR("MQTT 客户端初始化或连接失败: {}", ex.what());
		}
		catch (const _STD exception& ex)
		{
			LOG_ERROR("MQTT 客户端初始化或连接发生未知异常: {}", ex.what());
		}
		catch (...)
		{
			LOG_ERROR("MQTT 服务启动出现未知异常: <non-std exception>");
		}

		// 启动失败回滚: 尽力停止已建部分 (未创建成功的线程 joinable 为 false, 安全)
		this->impl_->runMaintain = false;
		this->impl_->maintainCv.notify_all();
		if (this->impl_->maintainThread.joinable())
		{
			this->impl_->maintainThread.join();
		}
		this->impl_->runSender = false;
		this->impl_->dequeCv.notify_one();
		if (this->impl_->senderThread.joinable())
		{
			this->impl_->senderThread.join();
		}
		if (this->system_event_remover_)
		{
			this->system_event_remover_.reset();
		}

		this->impl_.reset(new Impl());
		this->running_	 = false;
		this->connected_ = false;
		return false;
	}

	void MQTTv5Service::stop(void) noexcept
	{
		if (bool expected { true }; !this->running_.compare_exchange_strong(expected, false))
		{
			return;
		}

		// 1. 停止维护线程 (先于 client 销毁; join 等待可能正在进行的重连结束)
		this->impl_->runMaintain = false;
		this->impl_->maintainCv.notify_all();
		if (this->impl_->maintainThread.joinable())
		{
			this->impl_->maintainThread.join();
			LOG_DEBUG("MQTT 维护线程已停止");
		}

		// 2. 注销系统事件监听 (防止停止后仍收到 broker 更新事件)
		if (this->system_event_remover_)
		{
			this->system_event_remover_.reset();
			LOG_DEBUG("已注销 MQTT broker 更新事件监听");
		}

		// 3. 停止发送线程
		if (this->impl_->runSender.exchange(false))
		{
			this->impl_->dequeCv.notify_one();
			if (this->impl_->senderThread.joinable())
			{
				this->impl_->senderThread.join();
				LOG_DEBUG("MQTT 异步发送线程已停止");
			}
		}

		// 4. 断开并销毁 client
		_STD unique_ptr<_MQTT async_client> client {};
		{
			_STD lock_guard<_STD mutex> lock { this->impl_->clientMutex };
			client = _STD				move(this->impl_->client);
		}
		if (client)
		{
			try
			{
				this->impl_->manualDisconnect = true;
				if (client->is_connected())
				{
					LOG_INFO("正在断开 MQTT 连接");
					client->disconnect()->wait_for(_STD_CHRONO milliseconds(1000));
				}
			}
			catch (const _MQTT exception& ex)
			{
				LOG_ERROR("MQTTv5Service 停止异常（来自 MQTT）: {}", ex.what());
			}
			catch (const _STD exception& ex)
			{
				LOG_ERROR("MQTTv5Service 停止发生未知异常: {}", ex.what());
			}
			catch (...)
			{
				LOG_ERROR("MQTTv5Service 停止发生未知异常: <non-std exception>");
			}
			client.reset();
		}

		this->impl_.reset(new Impl());
		this->connected_ = false;
		LOG_INFO("MQTT 服务已停止");
	}

	void MQTTv5Service::restart(void) noexcept
	{
		LOG_INFO("正在请求重启 MQTT 服务");
		this->stop();
		_STD this_thread::sleep_for(_STD_CHRONO milliseconds(500));
		(void)this->start();
	}

	void MQTTv5Service::setBrokerUrlOverride(_STD string url) noexcept
	{
		bool		changed { false };
		_STD string effective {};
		{
			_STD lock_guard<_STD mutex> lock { this->mutex_ };
			if (this->broker_url_override_ != url)
			{
				this->broker_url_override_ = _STD move(url);
				effective				   = this->broker_url_override_;
				changed					   = true;
			}
		}

		if (changed)
		{
			LOG_INFO("MQTT broker 地址已更新为: {} (唤醒自检线程)", effective);
			this->impl_->maintainCv.notify_all();
		}
	}

	void MQTTv5Service::setConnected(bool status) noexcept
	{
		this->connected_.store(status, _STD memory_order_release);
	}

	bool MQTTv5Service::isConnected(void) const noexcept
	{
		return this->connected_.load(_STD memory_order_acquire);
	}

	MQTTv5Service& MQTTv5Service::getInstance(void) noexcept
	{
		static MQTTv5Service instance {};
		return instance;
	}

	bool MQTTv5Service::publish(_STD string_view topic, _STD string_view payload) noexcept
	{
		if (!this->impl_->runSender)
		{
			LOG_WARN("MQTT 发送服务未运行, 消息被丢弃");
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
					LOG_WARN(
						"MQTT 消息队列已满, 正在丢弃最旧的消息以保证数据新鲜度。此警告将在 {} 秒内抑制",
						this->LOG_THROTTLE_INTERVAL.count()
					);
					this->impl_->isDroppingMessages = true;
					this->impl_->lastDropLogTime	= now;
				}
				else
				{
					LOG_DEBUG("MQTT 消息队列已满, 丢弃最旧消息 (日志已抑制)");
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

	void MQTTv5Service::subscribe(_STD string_view topic) noexcept
	{
		_STD lock_guard<_STD mutex> lock { this->impl_->clientMutex };
		if (!this->isConnected() || !this->impl_->client)
		{
			LOG_WARN("MQTT 未连接, 对主题 '{}' 的订阅请求被忽略", topic);
			return;
		}

		try
		{
			this->impl_->client->subscribe(topic.data(), 1);
			LOG_DEBUG("已发送订阅主题 '{}' 的请求", topic);
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

	void MQTTv5Service::senderLoop(void) noexcept
	{
		LOG_DEBUG("MQTT 发送者线程循环开始");

		while (this->impl_->runSender)
		{
			_STD pair<_STD string, _STD string> message {};

			{
				_STD unique_lock<_STD mutex> lock(this->impl_->dequeMutex);
				this->impl_->dequeCv.wait(
					lock,
					[this]
					{
						return !this->impl_->messageDeque.empty() || !this->impl_->runSender;
					}
				);

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

			try
			{
				_STD lock_guard<_STD mutex> lock { this->impl_->clientMutex };
				if (!this->isConnected() || !this->impl_->client)
				{
					LOG_WARN("MQTT 未连接, 队列中的一条消息被丢弃");
					continue;
				}

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
				LOG_ERROR("发送者线程发布消息 '{}' 时发生未知异常，消息被丢弃", message.first);
			}
		}

		LOG_INFO("MQTT 发送者线程循环已结束");
	}

	// ============================ 维护线程 (自检 / 重连) ============================

	void MQTTv5Service::maintainLoop(void) noexcept
	{
		LOG_DEBUG("MQTT 维护线程循环开始 (自检周期 {}s)", kMaintainInterval.count());

		_STD unique_lock<_STD mutex> lock { this->impl_->maintainMutex };
		while (this->impl_->runMaintain)
		{
			// 周期等待; setBrokerUrlOverride() 与 stop() 会提前唤醒
			this->impl_->maintainCv.wait_for(lock, kMaintainInterval);
			if (!this->impl_->runMaintain)
			{
				break;
			}

			lock.unlock();
			this->ensureBrokerConnection();
			lock.lock();
		}

		LOG_INFO("MQTT 维护线程循环已结束");
	}

	void MQTTv5Service::ensureBrokerConnection(void) noexcept
	{
		const auto url { this->effectiveBrokerUrl() };
		if (url.empty())
		{
			// 地址未就绪 (静态配置为空且尚未收到目录广播): 保持等待, 下个周期复查
			LOG_DEBUG("MQTT broker 地址未就绪, 等待配置或目录服务发现");
			return;
		}

		{
			_STD lock_guard<_STD mutex> lock { this->impl_->clientMutex };
			if (this->isConnected() && this->impl_->activeUrl == url)
			{
				return; // 已连接且地址未变, 健康
			}

			// 同一地址的尝试仍在窗口内 (paho 异步连接尚未回调): 再等一个周期
			if (this->impl_->activeUrl == url && this->impl_->client &&
				(_STD_CHRONO steady_clock::now() - this->impl_->lastAttemptTime) < kConnectAttemptTimeout)
			{
				return;
			}
		}

		this->reconnectToBroker(url);
	}

	void MQTTv5Service::reconnectToBroker(const _STD string& url) noexcept
	{
		_STD unique_ptr<_MQTT async_client> old_client {};
		{
			_STD lock_guard<_STD mutex> lock { this->impl_->clientMutex };
			this->setConnected(false); // 先阻断订阅/发送使用旧 client, 再摘除
			old_client					 = _STD move(this->impl_->client);
			this->impl_->activeUrl		 = url;
			this->impl_->lastAttemptTime = _STD_CHRONO steady_clock::now();
		}

		// 旧 client 的断开与销毁放在锁外 (可能阻塞, 不拖住订阅/发送取锁)
		if (old_client)
		{
			try
			{
				if (old_client->is_connected())
				{
					old_client->disconnect()->wait_for(_STD_CHRONO milliseconds(1000));
				}
			}
			catch (...)
			{
				LOG_DEBUG("旧 MQTT 客户端断开时出现异常 (忽略)");
			}
			old_client.reset();
		}

		try
		{
			const auto client_id { plane::config::ConfigManager::getInstance().getMqttClientId() };
			auto	   client { _STD make_unique<_MQTT async_client>(url, client_id) };
			auto	   callback { _STD make_shared<MqttCallback>(this) };
			client->set_callback(*callback);

			_MQTT connect_options conn_opts {};
			conn_opts.set_keep_alive_interval(30);
			conn_opts.set_clean_session(true);
			conn_opts.set_automatic_reconnect(true);
			conn_opts.set_mqtt_version(MQTTVERSION_5);

			{
				_STD lock_guard<_STD mutex>	 lock { this->impl_->clientMutex };
				this->impl_->client	  = _STD   move(client);
				this->impl_->callback = _STD move(callback);
			}

			this->impl_->client->connect(conn_opts);
			LOG_INFO("MQTT 连接请求已发出: {}", url);
		}
		catch (const _MQTT exception& ex)
		{
			LOG_ERROR("MQTT broker '{}' 连接请求失败, 下个自检周期重试: {}", url, ex.what());
		}
		catch (const _STD exception& ex)
		{
			LOG_ERROR("MQTT broker '{}' 连接请求发生未知异常, 下个自检周期重试: {}", url, ex.what());
		}
		catch (...)
		{
			LOG_ERROR("MQTT broker '{}' 连接请求发生未知异常 <non-std>, 下个自检周期重试", url);
		}
	}

	_STD string MQTTv5Service::effectiveBrokerUrl(void) noexcept
	{
		{
			_STD lock_guard<_STD mutex> lock { this->mutex_ };
			if (!this->broker_url_override_.empty())
			{
				return this->broker_url_override_;
			}
		}
		return _STD string { plane::config::ConfigManager::getInstance().getMqttUrl() };
	}
} // namespace plane::manager

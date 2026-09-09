// cy_psdk/manager/catalog/CatalogManager.cpp

#include "manager/catalog/CatalogManager.h"

#include "config/ConfigManager.h"
#include "manager/mqtt/service/MQTTv5Service.h"
#include "utils/log_util/Logger.h"

#include <fmt/format.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#ifdef CATALOG_ENABLED
	#include <catalog_runtime.h>
#endif

namespace plane::manager
{
	// 实现体: 后台线程独占 runtime; appliedBrokerUrl 由事件回调(目录失联)与后台线程共同访问, 用 brokerMutex 保护
	struct CatalogManager::Impl
	{
		_STD_CHRONO milliseconds statusInterval { 10'000 };	 // 状态快照上报间隔
		_STD_CHRONO milliseconds heartbeatInterval { 3000 }; // 目录心跳间隔
		bool					 discoverBroker { false };	 // 是否用目录解析动态 broker
		_STD string				 brokerServiceId {};		 // 待解析的"中心"服务
		_STD string				 brokerProtocol { "mqtt" };	 // 取端点协议
		_STD string				 appliedBrokerUrl {};		 // 已应用(或已确认无需切换)的 broker
		_STD mutex				 brokerMutex {};
#ifdef CATALOG_ENABLED
		_STD unique_ptr<::swarm::catalog::CatalogRuntime> runtime {};
#endif

		Impl(void) noexcept			 = default;
		~Impl(void) noexcept		 = default;
		Impl(const Impl&)			 = delete;
		Impl& operator=(const Impl&) = delete;
	};
#ifdef CATALOG_ENABLED
	namespace
	{
		using CatalogRuntime		 = ::swarm::catalog::CatalogRuntime;
		using CatalogRuntimeOptions	 = ::swarm::catalog::CatalogRuntimeOptions;
		using CatalogEvent			 = ::swarm::catalog::CatalogEvent;
		using CatalogEventType		 = ::swarm::catalog::CatalogEventType;
		using CatalogState			 = ::swarm::catalog::CatalogState;
		using CatalogError			 = ::swarm::catalog::CatalogError;
		using ServiceQuery			 = ::swarm::catalog::ServiceQuery;
		using ServiceStatus			 = ::swarm::catalog::ServiceStatus;
		using ServiceComponentStatus = ::swarm::catalog::ServiceComponentStatus;
		using ServiceEndpoint		 = ::swarm::catalog::ServiceEndpoint;
		using ExposedPort			 = ::swarm::catalog::ExposedPort;
		using Ms					 = _STD_CHRONO	  milliseconds;
		using Clock					 = _STD_CHRONO steady_clock;

		_STD string				  stateText(CatalogState state) noexcept
		{
			switch (state)
			{
				case CatalogState::Stopped:
					return "Stopped";
				case CatalogState::Discovering:
					return "Discovering";
				case CatalogState::Discovered:
					return "Discovered";
				case CatalogState::Unavailable:
					return "Unavailable";
				case CatalogState::Conflict:
					return "Conflict";
				case CatalogState::Registering:
					return "Registering";
				case CatalogState::Ready:
					return "Ready";
				case CatalogState::Stopping:
					return "Stopping";
				default:
					return "Unknown";
			}
		}

		// 可被 stop() 打断的分段睡眠
		void sleepInterruptible(_STD atomic<bool>& running, Ms total) noexcept
		{
			Ms remaining { total };
			while (running.load() && remaining > Ms::zero())
			{
				const Ms step { _STD min(remaining, Ms { 100 }) };
				_STD	 this_thread::sleep_for(step);
				remaining -= step;
			}
		}
	} // namespace
#endif

	CatalogManager& CatalogManager::getInstance(void) noexcept
	{
		static CatalogManager instance {};
		return instance;
	}

	CatalogManager::CatalogManager(void) noexcept
	{
		this->impl_ = _STD make_unique<Impl>();
	}

	CatalogManager::~CatalogManager(void) noexcept
	{
		this->stop();
	}

	void CatalogManager::notifyPsdkRunning(bool running) noexcept
	{
		this->psdk_running_.store(running);
	}

	void CatalogManager::notifyHeartbeatRunning(bool running) noexcept
	{
		this->heartbeat_running_.store(running);
	}

	void CatalogManager::notifyTelemetryRunning(bool running) noexcept
	{
		this->telemetry_running_.store(running);
	}

	void CatalogManager::start(void) noexcept
	{
		if (this->started_.load(_STD memory_order_acquire))
		{
			return;
		}

		auto& config { plane::config::ConfigManager::getInstance() };
		if (!config.isCatalogEnabled())
		{
			LOG_DEBUG("SwarmCatalog 接入未启用 (config.yml catalog.enabled=false), 跳过");
			return;
		}

#ifdef CATALOG_ENABLED
		// 快照运行参数 (后台线程不再依赖配置变更)
		this->impl_->statusInterval	   = Ms { static_cast<long long>(config.getCatalogStatusReportIntervalMs()) };
		this->impl_->heartbeatInterval = Ms { static_cast<long long>(config.getCatalogHeartbeatIntervalMs()) };
		this->impl_->discoverBroker	   = config.isCatalogBrokerDiscoveryEnabled();
		this->impl_->brokerServiceId   = _STD string { config.getCatalogBrokerServiceId() };
		this->impl_->brokerProtocol	   = _STD  string { config.getCatalogBrokerPortProtocol() };

		LOG_INFO(
			"SwarmCatalog 接入启动: service_id='{}', service_name='{}', version='{}', discover_broker={}",
			config.getCatalogServiceId(),
			config.getCatalogServiceName(),
			config.getCatalogVersion(),
			this->impl_->discoverBroker
		);

		this->started_.store(true, _STD memory_order_release);
		this->running_.store(true);
		this->thread_ = _STD thread(&CatalogManager::runLoop, this);
#else
		LOG_ERROR("已配置启用 SwarmCatalog, 但本构建未编译目录客户端 (ENABLE_CATALOG_CLIENT=OFF)");
#endif
	}

	void CatalogManager::stop(void) noexcept
	{
		if (!this->started_.exchange(false, _STD memory_order_acq_rel))
		{
			this->running_.store(false);
			return;
		}

		this->running_.store(false, _STD memory_order_release);
		if (this->thread_.joinable())
		{
			this->thread_.join();
		}

		this->catalog_ready_.store(false);
		LOG_INFO("SwarmCatalog 接入已停止");
	}

#ifdef CATALOG_ENABLED
	void CatalogManager::runLoop(void) noexcept
	{
		auto& impl { *this->impl_ };

		// ---- 构造运行时 (仅保存业务参数, 不启动线程) ----
		CatalogRuntimeOptions options {};
		options.registration.namespace_name = "public";
		options.registration.group_name		= "DEFAULT_GROUP";
		options.registration.service_id		= plane::config::ConfigManager::getInstance().getCatalogServiceId();
		options.registration.service_name	= plane::config::ConfigManager::getInstance().getCatalogServiceName();
		options.registration.version		= plane::config::ConfigManager::getInstance().getCatalogVersion();
		options.heartbeat_interval			= impl.heartbeatInterval;
		options.event_callback				= [this, &impl](const CatalogEvent& ev)
		{
			const bool ready { ev.type == CatalogEventType::RegistrationSucceeded || ev.type == CatalogEventType::RegistrationRestored ||
							   (ev.type == CatalogEventType::StateChanged && ev.current_state == CatalogState::Ready) };
			const bool lost { ev.type == CatalogEventType::CatalogLost || ev.current_state == CatalogState::Unavailable };

			LOG_INFO(
				"Catalog 事件: type={}, state={} -> {}, message={}",
				static_cast<int>(ev.type),
				stateText(ev.previous_state),
				stateText(ev.current_state),
				ev.message
			);

			if (ready)
			{
				this->catalog_ready_.store(true);
				LOG_INFO("SwarmCatalog 注册成功 (Ready), 实例对外可见");
			}
			else if (lost)
			{
				this->catalog_ready_.store(false);
				// 目录失联/恢复后允许重新解析动态 broker (端点可能已变化)
				_STD lock_guard<_STD mutex> lock { impl.brokerMutex };
				impl.appliedBrokerUrl.clear();
			}
		};

		impl.runtime = _STD make_unique<CatalogRuntime>(_STD move(options));
		auto*				rt { impl.runtime.get() };

		// ---- start(): 读取 /etc/catalog.yml + 同步发现 (失败按可重试性退避) ----
		bool started { false };
		Ms	 backoff { 2000 };
		while (this->running_.load() && !started)
		{
			auto result { rt->start() };
			if (result.ok())
			{
				started = true;
				if (auto ep { rt->catalogEndpoint() }; ep)
				{
					LOG_INFO("SwarmCatalog 已发现: catalog {}:{} (node={})", ep->ip, ep->http_port, ep->node_name);
				}
				break;
			}

			const auto& err { result.error() };
			LOG_WARN("SwarmCatalog 启动失败: code={}, retryable={}, message={}", static_cast<int>(err.code), err.retryable, err.message);

			if (err.code == CatalogError::InvalidArgument)
			{
				// 配置缺失 /etc/catalog.yml 或注册参数非法 -> 不可重试, 降级退出
				LOG_ERROR("SwarmCatalog 启动参数非法 (板端需部署 /etc/catalog.yml), 本次放弃目录接入");
				break;
			}

			sleepInterruptible(this->running_, backoff);
			backoff = _STD min(backoff * 2, Ms { 60'000 });
		}

		if (!started)
		{
			(void)rt->stop(Ms { 1000 });
			impl.runtime.reset();
			this->running_.store(false);
			LOG_WARN("SwarmCatalog 接入未就绪, 已降级 (PSDK/MQTT 主链路不受影响)");
			return;
		}

		// ---- 请求注册 (实际注册与重试由库后台线程执行) ----
		(void)rt->registerServiceInstance();

		// ---- 主循环: 周期状态上报 + Ready 后单次动态 broker 解析 ----
		auto lastReport { Clock::now() };
		while (this->running_.load())
		{
			const bool ready { this->catalog_ready_.load() };
			if (ready && impl.discoverBroker)
			{
				bool needResolve { false };
				{
					_STD lock_guard<_STD mutex> lock { impl.brokerMutex };
					needResolve = impl.appliedBrokerUrl.empty();
				}
				if (needResolve)
				{
					this->trySwitchMqttBroker();
				}
			}

			const auto now { Clock::now() };
			if (now - lastReport >= impl.statusInterval)
			{
				this->reportStatus();
				lastReport = now;
			}

			_STD this_thread::sleep_for(Ms { 200 });
		}

		(void)rt->stop(Ms { 2000 });
		impl.runtime.reset();
		this->catalog_ready_.store(false);
		LOG_INFO("SwarmCatalog 后台线程已退出");
	}

	void CatalogManager::reportStatus(void) noexcept
	{
		auto& impl { *this->impl_ };
		if (!impl.runtime)
		{
			return;
		}

		auto&	   rt { *impl.runtime };
		const auto state { rt.state() };
		if (state != CatalogState::Ready)
		{
			LOG_DEBUG("SwarmCatalog 状态未就绪 (当前 {}), 跳过状态上报", stateText(state));
			return;
		}

		auto component = [](ServiceStatus& status, const _STD string& name, bool ok, const _STD string& message)
		{
			ServiceComponentStatus comp {};
			comp.name	 = name;
			comp.status	 = ok ? "UP" : "DOWN";
			comp.message = message;
			if (!ok)
			{
				status.healthy = false;
			}
			status.components.push_back(_STD move(comp));
		};

		ServiceStatus status {};
		status.healthy = true;
		status.message = "全部组件正常";

		const bool mqttOk { plane::manager::MQTTv5Service::getInstance().isConnected() };
		component(status, "catalog", true, "服务目录已连接");
		component(status, "mqtt", mqttOk, mqttOk ? "MQTT 已连接" : "MQTT 未连接");
		component(status, "psdk", this->psdk_running_.load(), this->psdk_running_.load() ? "PSDK 已就绪" : "PSDK 未就绪");
		component(status, "heartbeat", this->heartbeat_running_.load(), this->heartbeat_running_.load() ? "心跳正常" : "心跳未运行");
		component(status, "telemetry", this->telemetry_running_.load(), this->telemetry_running_.load() ? "遥测正常" : "遥测未运行");

		if (!status.healthy)
		{
			status.message = "部分组件异常, 详见 components";
		}

		auto result { rt.updateStatus(status) };
		if (!result.ok())
		{
			LOG_WARN("SwarmCatalog 状态上报失败: code={}, message={}", static_cast<int>(result.error().code), result.error().message);
		}
	}

	bool CatalogManager::trySwitchMqttBroker(void) noexcept
	{
		auto& impl { *this->impl_ };
		if (!impl.runtime || impl.brokerServiceId.empty())
		{
			return false;
		}

		ServiceQuery query {};
		query.service_id = impl.brokerServiceId; // namespace/group 为空 -> 继承注册作用域 (public/DEFAULT_GROUP)

		auto result { impl.runtime->resolveService(query) };
		if (!result.ok())
		{
			LOG_WARN(
				"目录解析中心服务失败: service_id='{}', code={}, message={}",
				impl.brokerServiceId,
				static_cast<int>(result.error().code),
				result.error().message
			);
			return false;
		}

		const auto&			   endpoints { result.value().endpoints };
		const ServiceEndpoint* selected { nullptr };
		for (const auto& endpoint : endpoints)
		{
			if (endpoint.primary)
			{
				selected = &endpoint;
				break;
			}
		}
		if (selected == nullptr && !endpoints.empty())
		{
			selected = &endpoints.front();
		}
		if (selected == nullptr)
		{
			LOG_WARN("目录解析中心服务无健康实例: service_id='{}'", impl.brokerServiceId);
			return false;
		}

		const ExposedPort* matched { nullptr };
		for (const auto& port : selected->exposed_ports)
		{
			if (port.protocol == impl.brokerProtocol)
			{
				matched = &port;
				break;
			}
		}
		if (matched == nullptr)
		{
			LOG_WARN("中心实例无 {} 端点: service_id='{}', instance='{}'", impl.brokerProtocol, impl.brokerServiceId, selected->instance_id);
			return false;
		}

		const auto& host { matched->ip.empty() ? selected->instance_ip : matched->ip };
		if (host.empty())
		{
			LOG_WARN("目录解析中心服务实例 IP 为空, 跳过 broker 切换");
			return false;
		}

		// 协议 -> URL scheme (mqtt/tcp 都按 tcp://; ws/wss/http/https 原样)
		_STD string scheme { "tcp" };
		if (impl.brokerProtocol == "ws")
		{
			scheme = "ws";
		}
		else if (impl.brokerProtocol == "wss")
		{
			scheme = "wss";
		}
		else if (impl.brokerProtocol == "http")
		{
			scheme = "http";
		}
		else if (impl.brokerProtocol == "https")
		{
			scheme = "https";
		}

		const auto discoveredUrl { _FMT format("{}://{}:{}", scheme, host, matched->port) };

		{
			_STD lock_guard<_STD mutex> lock { impl.brokerMutex };
			if (!impl.appliedBrokerUrl.empty())
			{
				return true; // 已应用过, 不再重复切换
			}
		}

		const auto staticUrl { _STD string { plane::config::ConfigManager::getInstance().getMqttUrl() } };
		if (discoveredUrl == staticUrl)
		{
			_STD lock_guard<_STD mutex> lock { impl.brokerMutex };
			impl.appliedBrokerUrl = discoveredUrl;
			LOG_INFO("目录解析的中心 broker 与静态配置一致: {}", discoveredUrl);
			return true;
		}

		LOG_INFO("目录解析到中心 MQTT broker: {} (静态配置: {}), 正在切换 MQTT 连接", discoveredUrl, staticUrl);

		plane::manager::MQTTv5Service::getInstance().setBrokerUrlOverride(discoveredUrl);
		plane::manager::MQTTv5Service::getInstance().restart();

		{
			_STD lock_guard<_STD mutex> lock { impl.brokerMutex };
			impl.appliedBrokerUrl = discoveredUrl;
		}
		return true;
	}
#else
	void CatalogManager::runLoop(void) noexcept {}

	void CatalogManager::reportStatus(void) noexcept {}

	bool CatalogManager::trySwitchMqttBroker(void) noexcept
	{
		return false;
	}
#endif
} // namespace plane::manager

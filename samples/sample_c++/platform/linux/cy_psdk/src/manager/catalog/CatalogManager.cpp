// cy_psdk/manager/catalog/CatalogManager.cpp
//
// SwarmCatalog 客户端接入 (自研 plane::catalog 运行时)。
// 后台线程: 发现 -> 注册 -> (运行时内部心跳/状态) -> Ready 后解析中心 mqtt 服务切换 broker。
// 发现参数 (node_id/port/targets) 来自 config.yml catalog 小节; 缺失则目录接入降级不启动。

#include "manager/catalog/CatalogManager.h"

#include <fmt/format.h>
#include <chrono>
#include <memory>
#include <string>

#include "config/ConfigManager.h"
#include "manager/catalog/client/CatalogModels.h"
#include "manager/catalog/client/CatalogRuntime.h"
#include "manager/catalog/client/CatalogRuntimeOptions.h"
#include "manager/catalog/client/CatalogTypes.h"
#include "manager/catalog/client/DiscoveryConfig.h"
#include "manager/catalog/client/internal/discovery/DiscoveryClient.h"
#include "manager/catalog/client/internal/transport/HttpTransport.h"
#include "manager/catalog/client/Result.h"
#include "manager/event_manager/EventManager.h"
#include "manager/mqtt/service/MQTTv5Service.h"
#include "manager/plane_state/PlaneStateStore.h"
#include "utils/log_util/Logger.h"

namespace plane::manager
{
	namespace
	{
		using CatalogRuntime		 = plane::catalog::CatalogRuntime;
		using CatalogRuntimeOptions	 = plane::catalog::CatalogRuntimeOptions;
		using CatalogEvent			 = plane::catalog::CatalogEvent;
		using CatalogEventType		 = plane::catalog::CatalogEventType;
		using CatalogState			 = plane::catalog::CatalogState;
		using CatalogEndpoint		 = plane::catalog::CatalogEndpoint;
		using ServiceStatus			 = plane::catalog::ServiceStatus;
		using ServiceComponentStatus = plane::catalog::ServiceComponentStatus;
		using ServiceQuery			 = plane::catalog::ServiceQuery;
		using ResolvedService		 = plane::catalog::ResolvedService;
		using ServiceEndpoint		 = plane::catalog::ServiceEndpoint;
		using ExposedPort			 = plane::catalog::ExposedPort;
		using ServiceRegistration	 = plane::catalog::ServiceRegistration;

		using Ms					 = _STD			   chrono::milliseconds;

		constexpr _STD string_view stateText(CatalogState state) noexcept
		{
			switch (state)
			{
				case CatalogState::STOPPED:
					return "Stopped";
				case CatalogState::DISCOVERING:
					return "Discovering";
				case CatalogState::DISCOVERED:
					return "Discovered";
				case CatalogState::UNAVAILABLE:
					return "Unavailable";
				case CatalogState::CONFLICT:
					return "Conflict";
				case CatalogState::REGISTERING:
					return "Registering";
				case CatalogState::READY:
					return "Ready";
				case CatalogState::STOPPING:
					return "Stopping";
			}
			return "Unknown";
		}

		// 端点协议 -> URL scheme (mqtt/tcp 按 tcp://; ws/wss/http/https/rtsp 原样)
		constexpr _STD string_view schemeForProtocol(const _STD string& protocol) noexcept
		{
			if (protocol == "ws")
			{
				return "ws";
			}
			if (protocol == "wss")
			{
				return "wss";
			}
			if (protocol == "http")
			{
				return "http";
			}
			if (protocol == "https")
			{
				return "https";
			}
			if (protocol == "rtsp")
			{
				return "rtsp";
			}
			return "tcp";
		}

		// 固定周期: 目录心跳与状态上报均由 CatalogRuntime 内部以 3s 驱动;
		// 业务壳仅周期(3s)调用 updateStatus 保持快照最新。
		constexpr Ms kStatusReportInterval { 3000 };

		// 中心 broker 地址解析: 未成功前快速重试, 成功后周期校验 (检测迁移/变更)
		constexpr auto kBrokerResolveRetryInterval { _STD_CHRONO seconds(5) };
		constexpr auto kBrokerResolveCheckInterval { _STD_CHRONO seconds(30) };
	} // namespace

	struct CatalogManager::Impl
	{
		// 自研目录运行时 (后台线程持有; 业务线程经 rt_mutex_ 取 shared_ptr 快照后锁外调用)
		_STD shared_ptr<CatalogRuntime> runtime {};

		// 最近一次 broker 解析尝试时刻与最近广播的地址 (仅业务壳线程访问;
		// 广播走 EventManager 系统事件, 由 MQTT 模块订阅后自治重连)
		_STD_CHRONO steady_clock::time_point last_broker_attempt {};
		_STD string							 last_published_url {};

		// 待解析的"中心"服务与端点协议 (来自 ConfigManager 固定值)
		_STD string broker_service_id {};
		_STD string broker_protocol {};
	};

	CatalogManager& CatalogManager::getInstance(void) noexcept
	{
		static CatalogManager instance {};
		return instance;
	}

	CatalogManager::CatalogManager(void) noexcept = default;

	CatalogManager::~CatalogManager(void) noexcept
	{
		this->stop();
	}

	void CatalogManager::notifyPsdkRunning(bool running) noexcept
	{
		this->psdk_running_.store(running, _STD memory_order_release);
	}

	void CatalogManager::notifyHeartbeatRunning(bool running) noexcept
	{
		this->heartbeat_running_.store(running, _STD memory_order_release);
	}

	void CatalogManager::notifyTelemetryRunning(bool running) noexcept
	{
		this->telemetry_running_.store(running, _STD memory_order_release);
	}

	void CatalogManager::start(void) noexcept
	{
		if (this->started_.exchange(true, _STD memory_order_acq_rel))
		{
			return;
		}

		auto& config { plane::config::ConfigManager::getInstance() };

		// 发现参数缺失: 无 node_id/targets 无法探测, 目录接入降级不启动 (不兜底)
		const _STD string node_id { config.getCatalogNodeId() };
		const auto&		  targets { config.getCatalogTargets() };
		if (node_id.empty() || targets.empty())
		{
			LOG_WARN("SwarmCatalog 发现参数缺失 (node_id/targets 未配置), 本次不启动目录接入");
			this->running_.store(false, _STD memory_order_release);
			// 同步域模型: 目录本次未启动 (供状态板显示真实状态, 避免长期显示“初始化中…”)
			plane::domain::PlaneStateStore::getInstance().update(
				[](plane::domain::PlaneStateDataClass& st)
				{
					st.catalog_state = "NotConfigured";
					st.catalog_ready = false;
				}
			);
			return;
		}

		this->impl_					   = _STD					 make_unique<Impl>();
		this->impl_->broker_service_id = _STD string { config.getCatalogBrokerServiceId() };
		this->impl_->broker_protocol   = _STD	string { config.getCatalogBrokerPortProtocol() };

		this->running_.store(true, _STD memory_order_release);
		this->thread_ = _STD thread(&CatalogManager::runLoop, this);
		LOG_INFO(
			"SwarmCatalog 接入启动: service_id='{}', service_name='{}', version='{}', discover_broker={}",
			config.getCatalogServiceId(),
			config.getCatalogServiceName(),
			config.getCatalogVersion(),
			config.isCatalogBrokerDiscoveryEnabled()
		);
	}

	void CatalogManager::stop(void) noexcept
	{
		if (!this->started_.exchange(false, _STD memory_order_acq_rel))
		{
			this->running_.store(false, _STD memory_order_release);
			return;
		}

		this->running_.store(false, _STD memory_order_release);
		if (this->thread_.joinable())
		{
			this->thread_.join();
		}

		this->catalog_ready_.store(false, _STD memory_order_release);
		LOG_INFO("SwarmCatalog 接入已停止");
	}

	void CatalogManager::runLoop(void) noexcept
	{
		auto& impl { *this->impl_ };
		auto& config { plane::config::ConfigManager::getInstance() };

		// 组装运行时: 注册信息 + 发现配置 + 事件回调
		CatalogRuntimeOptions options {};
		options.registration.namespace_name = "public";
		options.registration.group_name		= "DEFAULT_GROUP";
		options.registration.service_id		= config.getCatalogServiceId();
		options.registration.service_name	= config.getCatalogServiceName();
		options.registration.version		= config.getCatalogVersion();

		options.event_callback				= [this](const CatalogEvent& ev)
		{
			const bool ready { ev.type == CatalogEventType::REGISTRATION_SUCCEEDED || ev.type == CatalogEventType::REGISTRATION_RESTORED ||
							   (ev.type == CatalogEventType::STATE_CHANGED && ev.current_state == CatalogState::READY) };
			const bool lost { ev.type == CatalogEventType::CATALOG_LOST || ev.current_state == CatalogState::UNAVAILABLE };

			LOG_INFO(
				"Catalog 事件: type={}, state={} -> {}, message={}",
				static_cast<int>(ev.type),
				stateText(ev.previous_state),
				stateText(ev.current_state),
				ev.message
			);

			// 同步域模型: 目录状态 (经 mutator 只更新本模块字段; 供本地展示与上报)
			plane::domain::PlaneStateStore::getInstance().update(
				[&ev](plane::domain::PlaneStateDataClass& st)
				{
					st.catalog_state = stateText(ev.current_state);
					st.catalog_ready = (ev.current_state == CatalogState::READY);
				}
			);

			if (ready)
			{
				this->catalog_ready_.store(true, _STD memory_order_release);
				LOG_INFO("SwarmCatalog 注册成功 (Ready), 实例对外可见");
			}
			else if (lost)
			{
				this->catalog_ready_.store(false, _STD memory_order_release);
				// 目录失联/恢复后允许重新解析动态 broker (端点可能已变化)
				this->impl_->last_broker_attempt = {};
				this->impl_->last_published_url.clear();
			}
		};

		plane::catalog::DiscoveryConfig discovery {};
		discovery.node_id = config.getCatalogNodeId();
		discovery.port	  = static_cast<int>(config.getCatalogDiscoveryPort());
		discovery.targets = config.getCatalogTargets();

		impl.runtime	  = _STD make_shared<CatalogRuntime>(_STD move(options), _STD move(discovery));
		CatalogRuntime*		rt { impl.runtime.get() };

		// ---- 同步发现 (失败按可重试性退避; 参数非法不可重试则放弃) ----
		bool started { false };
		Ms	 backoff { 2000 };
		while (this->running_.load() && !started)
		{
			auto result { rt->start() };
			if (result.has_value())
			{
				started = true;
				if (auto ep { rt->catalogEndpoint() }; ep.has_value())
				{
					LOG_INFO("SwarmCatalog 已发现: catalog {}:{} (node={})", ep->ip, ep->http_port, ep->node_name);
				}
				break;
			}

			const auto& err { result.error() };
			LOG_WARN("SwarmCatalog 启动失败: code={}, retryable={}, message={}", static_cast<int>(err.code), err.retryable, err.message);

			if (err.code == plane::catalog::CatalogError::INVALID_ARGUMENT)
			{
				LOG_ERROR("SwarmCatalog 启动参数非法, 本次放弃目录接入");
				break;
			}

			// 分片睡眠: stop() 时最多等待 200ms 而非整个退避周期
			const auto deadline { _STD_CHRONO steady_clock::now() + backoff };
			while (this->running_.load(_STD memory_order_acquire) && _STD_CHRONO steady_clock::now() < deadline)
			{
				_STD this_thread::sleep_for(Ms { 200 });
			}
			backoff = _STD min(backoff * 2, Ms { 60'000 });
		}

		if (!started)
		{
			(void)rt->stop(Ms { 1000 });
			{
				_STD lock_guard<_STD mutex> lock { this->rt_mutex_ };
				impl.runtime.reset();
			}
			this->running_.store(false, _STD memory_order_release);
			LOG_WARN("SwarmCatalog 接入未就绪, 已降级 (PSDK/MQTT 主链路不受影响)");
			return;
		}

		// ---- 请求注册 (注册与重试/心跳由运行时内部控制线程执行) ----
		(void)rt->registerServiceInstance();

		// ---- 主循环: 周期状态上报 + Ready 后单次动态 broker 解析 ----
		auto lastReport { _STD_CHRONO steady_clock::now() };
		while (this->running_.load())
		{
			if (this->catalog_ready_.load(_STD memory_order_acquire) && config.isCatalogBrokerDiscoveryEnabled())
			{
				// 地址未广播前快速重试 (5s); 已广播后转为周期校验 (30s), 检测中心 broker 迁移
				const auto interval { impl.last_published_url.empty() ? kBrokerResolveRetryInterval : kBrokerResolveCheckInterval };
				const auto now { _STD_CHRONO steady_clock::now() };
				if (now - impl.last_broker_attempt >= interval)
				{
					impl.last_broker_attempt = now;
					(void)this->trySwitchMqttBroker();
				}
			}

			const auto now { _STD_CHRONO steady_clock::now() };
			if (now - lastReport >= kStatusReportInterval)
			{
				this->reportStatus();
				lastReport = now;
			}

			_STD this_thread::sleep_for(Ms { 200 });
		}

		(void)rt->stop(Ms { 2000 });
		{
			_STD lock_guard<_STD mutex> lock { this->rt_mutex_ };
			impl.runtime.reset();
		}
		this->catalog_ready_.store(false, _STD memory_order_release);
		LOG_INFO("SwarmCatalog 后台线程已退出");
	}

	void CatalogManager::reportStatus(void) noexcept
	{
		auto& impl { *this->impl_ };
		if (!impl.runtime)
		{
			return;
		}

		// 未 READY 时运行时内部不上报; 这里仅保持快照最新
		ServiceStatus status {};
		status.healthy	  = true;
		status.message	  = "全部组件正常";

		auto addComponent = [&status](const _STD string& name, bool ok, const _STD string& message)
		{
			ServiceComponentStatus component {};
			component.name	  = name;
			component.status  = ok ? "UP" : "DOWN";
			component.message = message;
			if (!ok)
			{
				status.healthy = false;
			}
			status.components.push_back(_STD move(component));
		};

		const bool mqtt_ok { plane::manager::MQTTv5Service::getInstance().isConnected() };
		addComponent("catalog", this->catalog_ready_.load(_STD memory_order_acquire), "服务目录已连接");
		addComponent("mqtt", mqtt_ok, mqtt_ok ? "MQTT 已连接" : "MQTT 未连接");
		addComponent("psdk", this->psdk_running_.load(_STD memory_order_acquire), this->psdk_running_.load() ? "PSDK 已就绪" : "PSDK 未就绪");
		addComponent("heartbeat", this->heartbeat_running_.load(_STD memory_order_acquire), "心跳服务状态");
		addComponent("telemetry", this->telemetry_running_.load(_STD memory_order_acquire), "遥测服务状态");

		if (!status.healthy)
		{
			status.message = "部分组件异常, 详见 components";
		}

		auto result { impl.runtime->updateStatus(status) };
		if (!result.has_value())
		{
			LOG_DEBUG("SwarmCatalog 状态上报未生效: code={}, message={}", static_cast<int>(result.error().code), result.error().message);
		}
	}

	bool CatalogManager::trySwitchMqttBroker(void) noexcept
	{
		auto& impl { *this->impl_ };
		if (!impl.runtime || impl.broker_service_id.empty())
		{
			return false;
		}

		ServiceQuery query {};
		query.service_id = impl.broker_service_id; // namespace/group 为空 -> 继承注册作用域 (public/DEFAULT_GROUP)

		auto result { impl.runtime->resolveService(query) };
		if (!result.has_value())
		{
			LOG_WARN(
				"目录解析中心服务失败: service_id='{}', code={}, message={}",
				impl.broker_service_id,
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
			LOG_WARN("目录解析中心服务无健康实例: service_id='{}'", impl.broker_service_id);
			return false;
		}

		const ExposedPort* matched { nullptr };
		for (const auto& port : selected->exposed_ports)
		{
			if (port.protocol == impl.broker_protocol)
			{
				matched = &port;
				break;
			}
		}
		if (matched == nullptr)
		{
			LOG_WARN("中心实例无 {} 端点: service_id='{}', instance='{}'", impl.broker_protocol, impl.broker_service_id, selected->instance_id);
			return false;
		}

		const auto& host { matched->ip.empty() ? selected->address : matched->ip };
		if (host.empty())
		{
			LOG_WARN("目录解析中心服务实例 IP 为空, 跳过 broker 切换");
			return false;
		}

		// 端点协议 -> URL scheme
		const auto scheme { schemeForProtocol(impl.broker_protocol) };
		const auto discovered_url { _FMT format("{}://{}:{}", scheme, host, matched->port) };

		const auto static_url { _STD string { plane::config::ConfigManager::getInstance().getMqttUrl() } };

		if (discovered_url == impl.last_published_url)
		{
			LOG_DEBUG("中心 broker 地址未变化, 跳过广播: {}", discovered_url);
			return true;
		}

		if (discovered_url == static_url)
		{
			LOG_INFO("目录解析的中心 broker 与静态配置一致: {}", discovered_url);
		}
		else
		{
			LOG_INFO("目录解析到中心 MQTT broker: {} (静态配置: {}), 已广播至模块总线", discovered_url, static_url);
		}

		// 解耦: 仅广播服务发现结果, 由 MQTT 模块订阅后通过自检线程完成(重)连接
		plane::manager::EventManager::getInstance()
			.publishSystemEvent(plane::manager::EventManager::SystemEvent::MqttBrokerUpdated, discovered_url);
		impl.last_published_url = discovered_url;
		return true;
	}

	// 目录服务端自身 IP (WebSocket 等直连场景使用); 未就绪/失败返回空串
	_STD string CatalogManager::getCatalogServerIp(void) noexcept
	{
		_STD shared_ptr<CatalogRuntime> runtime {};
		{
			_STD lock_guard<_STD mutex> lock { this->rt_mutex_ };
			if (!this->impl_ || !this->impl_->runtime)
			{
				return {};
			}
			runtime = this->impl_->runtime; // 取快照后在锁外调用 (HTTP), 避免长时间持锁
		}

		auto result { runtime->getCatalogServerInfo() };
		if (!result.has_value())
		{
			LOG_DEBUG("获取目录服务端信息失败: code={}, message={}", static_cast<int>(result.error().code), result.error().message);
			return {};
		}
		return result.value().ip;
	}

	_STD string CatalogManager::resolveServiceBaseUrl(const _STD string& service_id, const _STD string& protocol) noexcept
	{
		if (service_id.empty())
		{
			return {};
		}

		_STD shared_ptr<CatalogRuntime> runtime {};
		{
			_STD lock_guard<_STD mutex> lock { this->rt_mutex_ };
			if (!this->impl_ || !this->impl_->runtime)
			{
				return {};
			}
			runtime = this->impl_->runtime; // 取快照后在锁外调用 (HTTP), 避免长时间持锁
		}

		ServiceQuery query {};
		query.service_id = service_id; // namespace/group 为空 -> 继承注册作用域 (public/DEFAULT_GROUP)
		auto result { runtime->resolveService(query) };
		if (!result.has_value())
		{
			LOG_WARN(
				"目录解析服务失败: service_id='{}', code={}, message={}",
				service_id,
				static_cast<int>(result.error().code),
				result.error().message
			);
			return {};
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
			LOG_WARN("目录解析服务无健康实例: service_id='{}'", service_id);
			return {};
		}

		const ExposedPort* matched { nullptr };
		for (const auto& port : selected->exposed_ports)
		{
			if (port.protocol == protocol)
			{
				matched = &port;
				break;
			}
		}
		if (matched == nullptr)
		{
			LOG_WARN("服务实例无 {} 端点: service_id='{}', instance='{}'", protocol, service_id, selected->instance_id);
			return {};
		}

		const auto& host { matched->ip.empty() ? selected->address : matched->ip };
		if (host.empty())
		{
			LOG_WARN("服务实例 IP 为空: service_id='{}'", service_id);
			return {};
		}
		return _FMT format("{}://{}:{}", schemeForProtocol(protocol), host, matched->port);
	}

	void CatalogManager::updateServiceName(const _STD string& service_name) noexcept
	{
		_STD shared_ptr<CatalogRuntime> runtime {};
		{
			_STD lock_guard<_STD mutex> lock { this->rt_mutex_ };
			if (!this->impl_ || !this->impl_->runtime)
			{
				return;
			}
			runtime = this->impl_->runtime; // 取快照后在锁外调用 (HTTP), 避免长时间持锁
		}

		auto&				config { plane::config::ConfigManager::getInstance() };
		ServiceRegistration registration {};
		registration.namespace_name = "public";
		registration.group_name		= "DEFAULT_GROUP";
		registration.service_id		= config.getCatalogServiceId();
		registration.service_name	= service_name.empty() ? config.getCatalogServiceName() : service_name;
		registration.version		= config.getCatalogVersion();

		auto result { runtime->updateRegistration(registration) };
		if (!result.has_value())
		{
			LOG_WARN("Catalog 注册信息更新失败: code={}, message={}", static_cast<int>(result.error().code), result.error().message);
			return;
		}
		LOG_INFO("Catalog 注册信息已更新: service_name='{}'", registration.service_name);
	}
} // namespace plane::manager

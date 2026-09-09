// cy_psdk/manager/catalog/client/CatalogRuntime.cpp
//
// 自研 SwarmCatalog 客户端运行时实现 (由 swarm-catalog-client-java CatalogRuntime 转译)。
//
// 线程模型 (对齐 java):
//   - 控制线程: 每 10ms tick 一次, 驱动 周期重发现 / 注册重试 / 心跳 / 状态上报 / 配置轮询;
//   - 回调线程: 串行执行运行时事件与配置变更回调;
//   - 业务线程: 可并发调用查询/配置方法 (各自独立发 HTTP)。

#include "manager/catalog/client/CatalogRuntime.h"

#include <condition_variable>
#include <algorithm>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

#include "manager/catalog/client/CatalogError.h"
#include "manager/catalog/client/CatalogFailure.h"
#include "manager/catalog/client/internal/AvailabilityTracker.h"
#include "manager/catalog/client/internal/ConfigCache.h"
#include "manager/catalog/client/internal/DiscoveryClient.h"
#include "manager/catalog/client/internal/JsonCodec.h"
#include "manager/catalog/client/internal/RuntimeStateMachine.h"
#include "manager/catalog/client/internal/ServiceGateway.h"
#include "manager/catalog/client/internal/UdpDiscoveryClient.h"

#include "define.h"

namespace plane::catalog
{
	using internal::AvailabilityTracker;
	using internal::ConfigCache;
	using internal::DiscoveryClient;
	using internal::DiscoveryReport;
	using internal::DiscoveryStatus;
	using internal::RuntimeStateMachine;
	using internal::ServiceGateway;

	namespace
	{
		using Clock = _STD_CHRONO steady_clock;

		_NODISCARD _STD string	  scopeValue(const _STD string& value, const _STD string& fallback)
		{
			return value.empty() ? fallback : value;
		}

		_NODISCARD _STD_CHRONO milliseconds positiveInterval(_STD_CHRONO milliseconds value, _STD_CHRONO milliseconds fallback)
		{
			return value.count() > 0 ? value : fallback;
		}

		_NODISCARD _STD string catalogUrlOf(const CatalogEndpoint& endpoint)
		{
			return "http://" + endpoint.ip + ":" + _STD to_string(endpoint.http_port == 0 ? 8081 : endpoint.http_port);
		}

		_NODISCARD Result<CatalogFailure> invalidFailure(const _STD string& message)
		{
			return Result<CatalogFailure>::failure(makeFailure(CatalogError::INVALID_ARGUMENT, message));
		}
	} // namespace

	// ---- 实现体: 持有全部运行时状态与线程 ----
	struct CatalogRuntime::Impl
	{
		struct Watch
		{
			_STD function<void(const ConfigChangeEvent&)> callback {};
			_STD optional<ConfigDocument> last {};
		};

		CatalogRuntimeOptions options_ {};
		DiscoveryConfig		  discovery_config_ {};
		_STD unique_ptr<DiscoveryClient> discovery_ {};

		_STD unique_ptr<ServiceGateway> gateway_ {};

		// 状态 (mu_ 保护共享可变字段)
		mutable _STD mutex	mu_ {};
		RuntimeStateMachine state_machine_ {};
		AvailabilityTracker availability_ { 3 };
		ConfigCache			config_cache_ {};
		_STD optional<CatalogEndpoint> endpoint_ {};
		_STD string					   instance_id_ {};
		ServiceRegistration			   registration_ {};
		_STD optional<ServiceStatus> latest_status_ {};
		bool						 status_dirty_ { false };
		bool						 registration_requested_ { false };
		bool						 ever_started_ { false };
		_STD map<ConfigKey, Watch> watches_ {};

		// 调度 (仅控制线程写; 业务线程经 mu_ 读写)
		Clock::time_point		 next_registration_ { Clock::time_point::min() };
		Clock::time_point		 next_heartbeat_ { Clock::time_point::min() };
		Clock::time_point		 next_status_report_ { Clock::time_point::min() };
		Clock::time_point		 next_config_ { Clock::time_point::min() };
		Clock::time_point		 next_discovery_ { Clock::time_point::min() };
		_STD_CHRONO milliseconds retry_delay_ { 1000 };

		// 控制线程
		_STD atomic<bool> quit_ { false };
		_STD atomic<bool>		discovery_cancelled_ { false };
		_STD thread				control_thread_ {};
		_STD mutex				wake_mu_ {};
		_STD condition_variable wake_cv_ {};

		// 回调线程
		_STD atomic<bool>		cb_quit_ { false };
		_STD thread				cb_thread_ {};
		_STD mutex				cb_mu_ {};
		_STD condition_variable cb_cv_ {};
		_STD deque<_STD function<void()>> cb_queue_ {};

		explicit Impl(CatalogRuntimeOptions options, DiscoveryConfig discovery_config):
			options_(_STD move(options)),
			discovery_config_(_STD move(discovery_config)),
			availability_(this->options_.unavailable_failure_threshold > 0 ? this->options_.unavailable_failure_threshold : 3)
		{}

		_NODISCARD bool running(void) const
		{
			if (!this->ever_started_ || this->quit_.load(_STD memory_order_acquire))
			{
				return false;
			}
			const CatalogState state { this->state_machine_.state() };
			return state != CatalogState::STOPPED && state != CatalogState::STOPPING;
		}

		_NODISCARD _STD_CHRONO milliseconds heartbeatInterval(void) const
		{
			return positiveInterval(this->options_.heartbeat_interval, _STD_CHRONO milliseconds { 3000 });
		}

		_NODISCARD _STD_CHRONO milliseconds configCheckInterval(void) const
		{
			return positiveInterval(this->options_.config_check_interval, _STD_CHRONO milliseconds { 5000 });
		}

		_NODISCARD _STD_CHRONO milliseconds httpTimeout(void) const
		{
			return positiveInterval(this->options_.http_timeout, _STD_CHRONO milliseconds { 10'000 });
		}

		_NODISCARD CatalogFailure gateFailure(void) const
		{
			if (!this->ever_started_)
			{
				return makeFailure(CatalogError::NOT_STARTED);
			}
			switch (this->state_machine_.state())
			{
				case CatalogState::CONFLICT:
					return makeFailure(CatalogError::CATALOG_CONFLICT);
				case CatalogState::STOPPED:
				case CatalogState::STOPPING:
					return makeFailure(CatalogError::STOPPED);
				case CatalogState::DISCOVERING:
					return makeFailure(CatalogError::CATALOG_UNAVAILABLE, _STD string { defaultErrorMessage(CatalogError::NOT_STARTED) });
				default:
					return makeFailure(CatalogError::CATALOG_UNAVAILABLE);
			}
		}

		// ---- 回调投递 ----
		void dispatch(_STD function<void()> task)
		{
			{
				_STD lock_guard<_STD mutex> lock { this->cb_mu_ };
				this->cb_queue_.push_back(_STD move(task));
			}
			this->cb_cv_.notify_one();
		}

		void postEvent(
			CatalogEventType   type,
			CatalogState	   previous,
			CatalogState	   current,
			const _STD string& message,
			_STD optional<CatalogEndpoint> event_endpoint,
			_STD vector<CatalogEndpoint> conflicts
		)
		{
			if (!this->options_.event_callback)
			{
				return;
			}
			CatalogEvent event {};
			event.type				 = type;
			event.previous_state	 = previous;
			event.current_state		 = current;
			event.message			 = message;
			event.endpoint			 = _STD			  move(event_endpoint);
			event.conflict_endpoints = _STD move(conflicts);
			this->dispatch(
				[callback = this->options_.event_callback, event = _STD move(event)]() mutable
				{
					callback(event);
				}
			);
		}

		void transition(CatalogState next, CatalogEventType type, const _STD string& message)
		{
			const CatalogState previous { this->state_machine_.state() };
			this->state_machine_.set(next);
			if (this->gateway_)
			{
				this->gateway_->setConflict(next == CatalogState::CONFLICT);
				this->gateway_->setStopping(next == CatalogState::STOPPING);
				this->gateway_
					->setRemoteAllowed(next == CatalogState::DISCOVERED || next == CatalogState::REGISTERING || next == CatalogState::READY);
			}
			_STD optional<CatalogEndpoint> ep {};
			{
				_STD lock_guard<_STD mutex> lock { this->mu_ };
				ep = this->endpoint_;
			}
			this->postEvent(type, previous, next, message, _STD move(ep), {});
		}

		void kick(void)
		{
			this->wake_cv_.notify_one();
		}

		// ---- 控制线程 ----
		void runControl(void)
		{
			for (;;)
			{
				{
					_STD unique_lock<_STD mutex> lock { this->wake_mu_ };
					this->wake_cv_.wait_for(
						lock,
						_STD_CHRONO milliseconds { 10 },
						[this]
						{
							return this->quit_.load(_STD memory_order_acquire);
						}
					);
				}
				if (this->quit_.load(_STD memory_order_acquire))
				{
					break;
				}
				this->tick();
			}
		}

		void runCallbacks(void)
		{
			for (;;)
			{
				_STD function<void()> task {};
				{
					_STD unique_lock<_STD mutex> lock { this->cb_mu_ };
					this->cb_cv_.wait(
						lock,
						[this]
						{
							return this->cb_quit_.load(_STD memory_order_acquire) || !this->cb_queue_.empty();
						}
					);
					if (this->cb_queue_.empty())
					{
						if (this->cb_quit_.load(_STD memory_order_acquire))
						{
							break;
						}
						continue;
					}
					task = _STD move(this->cb_queue_.front());
					this->cb_queue_.pop_front();
				}
				try
				{
					task();
				}
				catch (...)
				{
					// 回调异常被吞掉, 不终止运行时
				}
			}
		}

		// ---- tick 驱动 (对齐 java tick/tickSafely) ----
		void tick(void)
		{
			try
			{
				this->tickOnce();
			}
			catch (...)
			{
				const CatalogState state { this->state_machine_.state() };
				this->postEvent(CatalogEventType::STATE_CHANGED, state, state, "tick failure", {}, {});
			}
		}

		void tickOnce(void)
		{
			if (!this->running())
			{
				return;
			}
			const Clock::time_point now { Clock::now() };

			if (now >= this->next_discovery_)
			{
				this->refreshDiscovery();
				this->next_discovery_ =
					Clock::now() + (this->state_machine_.state() == CatalogState::READY ? this->discovery_config_.ready_probe_interval
																						: this->discovery_config_.response_window);
			}
			if (this->state_machine_.allowRegister() && this->registration_requested_ && this->instance_id_.empty() &&
				now >= this->next_registration_)
			{
				this->registerOnce();
			}
			if (this->state_machine_.allowStatusReport() && !this->instance_id_.empty() && now >= this->next_heartbeat_)
			{
				this->heartbeatOnce();
			}
			if (this->state_machine_.allowStatusReport() && this->status_dirty_ && now >= this->next_status_report_)
			{
				this->reportLatestStatus();
			}
			if (!this->watches_.empty() && this->state_machine_.allowConfigRefresh() && now >= this->next_config_)
			{
				this->pollConfigs();
			}
		}

		// ---- 注册 / 心跳 / 状态 / 配置 ----
		void registerOnce(void)
		{
			if (!this->gateway_)
			{
				return;
			}
			ServiceRegistration registration { this->registrationSnapshot() };
			Result<_STD string> result { this->gateway_->registerInstance(registration) };
			if (result.isOk() && !result.value().empty())
			{
				{
					_STD lock_guard<_STD mutex> lock { this->mu_ };
					this->instance_id_ = result.value();
				}
				_STD optional<CatalogEndpoint> ep { this->endpointSnapshot() };
				if (ep.has_value())
				{
					this->discovery_->saveSuccessfulIp(ep->ip);
				}
				this->retry_delay_ = _STD_CHRONO milliseconds { 1000 };
				this->availability_.success();
				this->next_heartbeat_ = Clock::now() + this->heartbeatInterval();
				this->transition(CatalogState::READY, CatalogEventType::REGISTRATION_SUCCEEDED, "registered");
			}
			else
			{
				const CatalogFailure& error { result.error() };
				this->postEvent(
					CatalogEventType::REGISTRATION_FAILED,
					this->state_machine_.state(),
					this->state_machine_.state(),
					error.message,
					this->endpointSnapshot(),
					{}
				);
				if (error.retryable)
				{
					this->retry_delay_		 = _STD min(this->retry_delay_ * 2, _STD_CHRONO milliseconds { 2000 });
					this->next_registration_ = Clock::now() + this->retry_delay_;
				}
				else
				{
					this->next_registration_ = Clock::now() + _STD_CHRONO seconds(2);
				}
				this->noteCatalogFailure(error);
			}
		}

		void heartbeatOnce(void)
		{
			if (!this->gateway_)
			{
				return;
			}
			ServiceRegistration registration { this->registrationSnapshot() };
			const _STD string	id { this->instanceId() };
			Result<void>		result { this->gateway_->heartbeat(registration, id) };
			this->next_heartbeat_ = Clock::now() + this->heartbeatInterval();
			if (result.isOk())
			{
				this->availability_.success();
				return;
			}
			const CatalogFailure& error { result.error() };
			this->postEvent(
				CatalogEventType::STATUS_REPORT_FAILED,
				this->state_machine_.state(),
				this->state_machine_.state(),
				error.message,
				this->endpointSnapshot(),
				{}
			);
			if (error.code == CatalogError::INSTANCE_NOT_FOUND || error.code == CatalogError::SERVICE_NOT_FOUND)
			{
				this->resetInstanceForReregister("instance lost");
			}
			else
			{
				this->noteCatalogFailure(error);
			}
		}

		void reportLatestStatus(void)
		{
			_STD optional<ServiceStatus> snapshot {};
			{
				_STD lock_guard<_STD mutex> lock { this->mu_ };
				snapshot			= this->latest_status_;
				this->status_dirty_ = false;
			}
			const _STD string id { this->instanceId() };
			if (!snapshot.has_value() || id.empty())
			{
				{
					_STD lock_guard<_STD mutex> lock { this->mu_ };
					this->status_dirty_ = true;
				}
				return;
			}
			ServiceRegistration registration { this->registrationSnapshot() };
			Result<void>		result { this->gateway_->reportStatus(registration, id, *snapshot) };
			if (result.isOk())
			{
				this->availability_.success();
				this->next_status_report_ = Clock::time_point::min();
				return;
			}
			{
				_STD lock_guard<_STD mutex> lock { this->mu_ };
				this->status_dirty_ = true;
			}
			this->next_status_report_ = Clock::now() + _STD_CHRONO seconds(2);
			const CatalogFailure&								   error { result.error() };
			this->postEvent(
				CatalogEventType::STATUS_REPORT_FAILED,
				this->state_machine_.state(),
				this->state_machine_.state(),
				error.message,
				this->endpointSnapshot(),
				{}
			);
			if (error.code == CatalogError::INSTANCE_NOT_FOUND || error.code == CatalogError::SERVICE_NOT_FOUND)
			{
				this->resetInstanceForReregister("instance lost");
				this->next_status_report_ = Clock::time_point::min(); // 重注册成功后立即补报
			}
			else
			{
				this->noteCatalogFailure(error);
			}
		}

		void resetInstanceForReregister(const _STD string& message)
		{
			{
				_STD lock_guard<_STD mutex> lock { this->mu_ };
				this->instance_id_.clear();
			}
			this->retry_delay_		 = _STD_CHRONO milliseconds { 1000 };
			this->next_registration_ = Clock::time_point::min();
			this->transition(CatalogState::REGISTERING, CatalogEventType::STATE_CHANGED, message);
		}

		void pollConfigs(void)
		{
			_STD vector<_STD pair<ConfigKey, Watch>> snapshot {};
			{
				_STD lock_guard<_STD mutex> lock { this->mu_ };
				for (const auto& [key, watch] : this->watches_)
				{
					snapshot.emplace_back(key, watch);
				}
			}
			for (auto& [key, watch] : snapshot)
			{
				Result<ConfigDocument> result { this->gateway_->getConfig(key) };
				if (!result.isOk())
				{
					this->postEvent(
						CatalogEventType::CONFIG_FETCH_FAILED,
						this->state_machine_.state(),
						this->state_machine_.state(),
						result.error().message,
						this->endpointSnapshot(),
						{}
					);
					continue;
				}
				ConfigDocument current { result.value() };
				const bool	   changed { !watch.last.has_value() || watch.last->version != current.version ||
										 watch.last->content != current.content };
				if (!changed)
				{
					continue;
				}
				ConfigDocument previous { watch.last.value_or(ConfigDocument {}) };
				{
					_STD lock_guard<_STD mutex> lock { this->mu_ };
					watch.last				 = current;
					this->watches_[key].last = current;
				}
				this->config_cache_.remember(key, current);
				const bool		  initial_load { previous.version.empty() && previous.content.empty() };
				ConfigChangeEvent change {};
				change.previous		= previous;
				change.current		= current;
				change.initial_load = initial_load;
				this->dispatch(
					[callback = watch.callback, change = _STD move(change)]() mutable
					{
						callback(change);
					}
				);
			}
			this->next_config_ = Clock::now() + this->configCheckInterval();
		}

		void publishConfig(const ConfigKey& key, const ConfigDocument& current)
		{
			Watch watch {};
			{
				_STD lock_guard<_STD mutex> lock { this->mu_ };
				const auto					it { this->watches_.find(key) };
				if (it == this->watches_.end())
				{
					return;
				}
				watch = it->second;
			}
			const bool changed { !watch.last.has_value() || watch.last->version != current.version || watch.last->content != current.content };
			if (!changed)
			{
				return;
			}
			ConfigDocument previous { watch.last.value_or(ConfigDocument {}) };
			{
				_STD lock_guard<_STD mutex> lock { this->mu_ };
				this->watches_[key].last = current;
			}
			this->config_cache_.remember(key, current);
			ConfigChangeEvent change {};
			change.previous		= previous;
			change.current		= current;
			change.initial_load = previous.version.empty() && previous.content.empty();
			this->dispatch(
				[callback = watch.callback, change = _STD move(change)]() mutable
				{
					callback(change);
				}
			);
		}

		// ---- 周期重发现 ----
		void refreshDiscovery(void)
		{
			const CatalogState	  previous { this->state_machine_.state() };
			const DiscoveryReport report { this->discovery_->discover(this->discovery_config_, this->discovery_cancelled_) };

			if (report.multiple_instances || report.endpoints.size() > 1)
			{
				_STD optional<CatalogEndpoint> old {};
				{
					_STD lock_guard<_STD mutex> lock { this->mu_ };
					old = this->endpoint_;
					this->endpoint_.reset();
					this->instance_id_.clear();
				}
				if (this->gateway_)
				{
					this->gateway_->setCatalogUrl("");
					this->gateway_->setConflict(true);
					this->gateway_->setRemoteAllowed(false);
				}
				this->state_machine_.set(CatalogState::CONFLICT);
				this->postEvent(
					CatalogEventType::DISCOVERY_CONFLICT,
					previous,
					CatalogState::CONFLICT,
					"multiple catalog instances",
					old,
					report.endpoints
				);
				return;
			}

			if (report.endpoints.empty())
			{
				{
					_STD lock_guard<_STD mutex> lock { this->mu_ };
					this->endpoint_.reset();
					this->instance_id_.clear();
				}
				if (this->gateway_)
				{
					this->gateway_->setCatalogUrl("");
					this->gateway_->setRemoteAllowed(false);
				}
				this->transition(
					CatalogState::UNAVAILABLE,
					previous == CatalogState::UNAVAILABLE ? CatalogEventType::CATALOG_LOST : CatalogEventType::DISCOVERY_FAILED,
					report.error.empty() ? "catalog not found" : report.error
				);
				return;
			}

			const CatalogEndpoint discovered { report.endpoints.front() };
			const _STD string	  url { catalogUrlOf(discovered) };
			const bool			  unchanged { previous != CatalogState::CONFLICT && previous != CatalogState::UNAVAILABLE &&
											  previous != CatalogState::DISCOVERING && this->gateway_ && this->gateway_->catalogUrl() == url };

			{
				_STD lock_guard<_STD mutex> lock { this->mu_ };
				this->endpoint_ = discovered;
			}
			if (this->gateway_)
			{
				this->gateway_->setCatalogUrl(url);
				this->gateway_->setConflict(false);
				this->gateway_->setStopping(false);
				this->gateway_->setRemoteAllowed(true);
			}
			this->availability_.reset();

			if (!unchanged)
			{
				bool requested { false };
				{
					_STD lock_guard<_STD mutex> lock { this->mu_ };
					requested = this->registration_requested_;
					if (requested)
					{
						this->instance_id_.clear();
					}
				}
				const CatalogState next { requested ? CatalogState::REGISTERING : CatalogState::DISCOVERED };
				this->state_machine_.set(next);
				this->postEvent(
					previous == CatalogState::CONFLICT || previous == CatalogState::UNAVAILABLE ? CatalogEventType::REGISTRATION_RESTORED
																								: CatalogEventType::DISCOVERY_SUCCEEDED,
					previous,
					next,
					"catalog discovered",
					discovered,
					{}
				);
				this->next_registration_ = Clock::time_point::min();
			}
		}

		void noteCatalogFailure(const CatalogFailure& error)
		{
			const bool relevant { error.code == CatalogError::TIMEOUT || error.code == CatalogError::CATALOG_UNAVAILABLE ||
								  (error.code == CatalogError::HTTP_ERROR && error.retryable) };
			if (!relevant || !this->availability_.retryableFailureTrips())
			{
				return;
			}
			{
				_STD lock_guard<_STD mutex> lock { this->mu_ };
				this->instance_id_.clear();
			}
			this->transition(CatalogState::UNAVAILABLE, CatalogEventType::CATALOG_LOST, error.message);
			this->next_discovery_ = Clock::time_point::min();
		}

		// ---- 快照访问 ----
		_NODISCARD ServiceRegistration registrationSnapshot(void) const
		{
			_STD lock_guard<_STD mutex> lock { this->mu_ };
			return this->registration_;
		}

		_NODISCARD _STD optional<CatalogEndpoint> endpointSnapshot(void) const
		{
			_STD lock_guard<_STD mutex> lock { this->mu_ };
			return this->endpoint_;
		}

		_NODISCARD _STD string instanceId(void) const
		{
			_STD lock_guard<_STD mutex> lock { this->mu_ };
			return this->instance_id_;
		}

		// ---- 配置/查询辅助 ----
		_NODISCARD _STD optional<_STD vector<ConfigDocument>> allCached(const ConfigQuery& query)
		{
			_STD vector<ConfigDocument> documents {};
			for (const auto& data_id : query.data_ids)
			{
				const ConfigKey key { query.namespace_name, query.group_name, data_id };
				const _STD optional<ConfigDocument> cached { this->config_cache_.cached(key) };
				if (!cached.has_value())
				{
					return _STD nullopt;
				}
				documents.push_back(*cached);
			}
			return documents;
		}
	};

	// ==================== CatalogRuntime 公开方法 ====================

	CatalogRuntime::CatalogRuntime(
		CatalogRuntimeOptions options,
		DiscoveryConfig		  discovery_config,
		_STD unique_ptr<internal::HttpTransport> http_transport,
		_STD unique_ptr<internal::DiscoveryClient> discovery_client
	):
		impl_(_STD make_unique<Impl>(_STD move(options), _STD move(discovery_config)))
	{
		// 归一化注册信息 (namespace/group 默认 public/DEFAULT_GROUP, version 去除首尾空白)
		{
			_STD lock_guard<_STD mutex> lock { impl_->mu_ };
			impl_->registration_				= impl_->options_.registration;
			impl_->registration_.namespace_name = scopeValue(impl_->registration_.namespace_name, "public");
			impl_->registration_.group_name		= scopeValue(impl_->registration_.group_name, "DEFAULT_GROUP");
			Result<_STD string> version { internal::JsonCodec::normalizeVersion(impl_->registration_.version) };
			if (version.isOk())
			{
				impl_->registration_.version = version.value();
			}
		}
		impl_->discovery_ = discovery_client ? _STD move(discovery_client) : _STD make_unique<internal::UdpDiscoveryClient>();
		(void)http_transport; // 传输经 ServiceGateway 注入; 为空时 ServiceGateway 使用默认实现
	}

	CatalogRuntime::~CatalogRuntime(void)
	{
		try
		{
			this->stop(_STD_CHRONO seconds(2));
		}
		catch (...)
		{}
	}

	Result<void> CatalogRuntime::start(void)
	{
		bool already_started { false };
		{
			_STD lock_guard<_STD mutex> lock { impl_->mu_ };
			already_started = impl_->ever_started_;
		}
		if (already_started)
		{
			return Result<void>::failure(makeFailure(CatalogError::ALREADY_STARTED));
		}

		// 注册参数校验
		ServiceRegistration registration { impl_->registrationSnapshot() };
		if (registration.service_id.empty())
		{
			return Result<void>::failure(makeFailure(CatalogError::INVALID_ARGUMENT, "service_id is empty"));
		}
		if (registration.service_name.empty())
		{
			return Result<void>::failure(makeFailure(CatalogError::INVALID_ARGUMENT, "service_name is empty"));
		}
		Result<_STD string> version { internal::JsonCodec::normalizeVersion(registration.version) };
		if (!version.isOk())
		{
			return Result<void>::failure(version.error());
		}

		impl_->discovery_cancelled_.store(false, _STD memory_order_release);
		const DiscoveryReport report { impl_->discovery_->discover(impl_->discovery_config_, impl_->discovery_cancelled_) };

		if (report.multiple_instances || report.endpoints.size() > 1)
		{
			impl_->state_machine_.set(CatalogState::STOPPED);
			return Result<void>::failure(makeFailure(CatalogError::CATALOG_CONFLICT, "multiple catalog instances"));
		}
		if (report.endpoints.empty())
		{
			impl_->state_machine_.set(CatalogState::STOPPED);
			const CatalogError code { report.status == DiscoveryStatus::INVALID_ARGUMENT ? CatalogError::INVALID_ARGUMENT
																						 : CatalogError::DISCOVERY_TIMEOUT };
			return Result<void>::failure(makeFailure(code, report.error));
		}

		const CatalogEndpoint discovered { report.endpoints.front() };
		{
			_STD lock_guard<_STD mutex> lock { impl_->mu_ };
			impl_->endpoint_ = discovered;
		}
		impl_->gateway_ = _STD make_unique<ServiceGateway>(catalogUrlOf(discovered), nullptr, impl_->httpTimeout());
		impl_->gateway_->setRemoteAllowed(true);
		impl_->state_machine_.set(CatalogState::DISCOVERED);

		impl_->cb_quit_.store(false, _STD memory_order_release);
		impl_->cb_thread_ = _STD thread(&Impl::runCallbacks, impl_.get());
		impl_->quit_.store(false, _STD memory_order_release);
		impl_->control_thread_ = _STD thread(&Impl::runControl, impl_.get());

		const Clock::time_point		  now { Clock::now() };
		{
			_STD lock_guard<_STD mutex> lock { impl_->mu_ };
			impl_->ever_started_   = true;
			impl_->next_config_	   = now;
			impl_->next_discovery_ = now + impl_->discovery_config_.ready_probe_interval;
		}
		impl_->kick();
		return Result<void>::success();
	}

	Result<void> CatalogRuntime::registerServiceInstance(void)
	{
		if (!impl_->running())
		{
			return Result<void>::failure(makeFailure(CatalogError::NOT_STARTED));
		}
		const CatalogState state { impl_->state_machine_.state() };
		if (state == CatalogState::CONFLICT)
		{
			return Result<void>::failure(makeFailure(CatalogError::CATALOG_CONFLICT));
		}
		if (state == CatalogState::UNAVAILABLE)
		{
			return Result<void>::failure(makeFailure(CatalogError::CATALOG_UNAVAILABLE));
		}
		if (state == CatalogState::STOPPING || state == CatalogState::STOPPED)
		{
			return Result<void>::failure(makeFailure(CatalogError::STOPPED));
		}
		{
			_STD lock_guard<_STD mutex> lock { impl_->mu_ };
			impl_->registration_requested_ = true;
		}
		if (state == CatalogState::DISCOVERED)
		{
			impl_->transition(CatalogState::REGISTERING, CatalogEventType::STATE_CHANGED, "registration requested");
		}
		impl_->next_registration_ = Clock::time_point::min();
		impl_->kick();
		return Result<void>::success();
	}

	Result<void> CatalogRuntime::stop(_STD_CHRONO milliseconds timeout)
	{
		{
			_STD lock_guard<_STD mutex> lock { impl_->mu_ };
			if (!impl_->ever_started_ || impl_->state_machine_.state() == CatalogState::STOPPED)
			{
				return Result<void>::failure(makeFailure(CatalogError::NOT_STARTED));
			}
		}
		impl_->state_machine_.set(CatalogState::STOPPING);
		impl_->discovery_cancelled_.store(true, _STD memory_order_release);
		if (impl_->gateway_)
		{
			impl_->gateway_->setStopping(true);
			impl_->gateway_->setRemoteAllowed(false);
		}
		impl_->quit_.store(true, _STD memory_order_release);
		impl_->wake_cv_.notify_all();
		if (impl_->control_thread_.joinable())
		{
			impl_->control_thread_.join();
		}

		{
			_STD lock_guard<_STD mutex> lock { impl_->cb_mu_ };
			impl_->cb_quit_.store(true, _STD memory_order_release);
		}
		impl_->cb_cv_.notify_all();
		if (impl_->cb_thread_.joinable())
		{
			impl_->cb_thread_.join();
		}

		impl_->state_machine_.set(CatalogState::STOPPED);

		// best-effort 注销 (需要临时放开网关)
		const _STD string instance_id { impl_->instanceId() };
		if (!instance_id.empty() && impl_->gateway_)
		{
			impl_->gateway_->setStopping(false);
			impl_->gateway_->setConflict(false);
			impl_->gateway_->setRemoteAllowed(true);
			impl_->gateway_->setTimeout(positiveInterval(timeout, _STD_CHRONO seconds(2)));
			(void)impl_->gateway_->deleteInstance(impl_->registrationSnapshot(), instance_id);
		}
		return Result<void>::success();
	}

	CatalogState CatalogRuntime::state(void) const
	{
		return impl_->state_machine_.state();
	}

	_STD optional<CatalogEndpoint> CatalogRuntime::catalogEndpoint(void) const
	{
		return impl_->endpointSnapshot();
	}

	_STD string CatalogRuntime::instanceId(void) const
	{
		return impl_->instanceId();
	}

	Result<void> CatalogRuntime::updateStatus(const ServiceStatus& status)
	{
		if (!impl_->running())
		{
			return Result<void>::failure(makeFailure(CatalogError::NOT_STARTED));
		}
		{
			_STD lock_guard<_STD mutex> lock { impl_->mu_ };
			impl_->latest_status_	   = status;
			impl_->status_dirty_	   = true;
			impl_->next_status_report_ = Clock::time_point::min();
		}
		impl_->kick();
		return Result<void>::success();
	}

	Result<ResolvedService> CatalogRuntime::resolveService(const ServiceQuery& query)
	{
		if (!impl_->state_machine_.allowQuery())
		{
			return Result<ResolvedService>::failure(impl_->gateFailure());
		}
		const ServiceRegistration registration { impl_->registrationSnapshot() };
		ServiceQuery			  scoped {};
		scoped.namespace_name = scopeValue(query.namespace_name, registration.namespace_name);
		scoped.group_name	  = scopeValue(query.group_name, registration.group_name);
		scoped.service_id	  = query.service_id;
		scoped.service_name	  = query.service_name;
		if (!impl_->gateway_)
		{
			return Result<ResolvedService>::failure(makeFailure(CatalogError::CATALOG_UNAVAILABLE));
		}
		return impl_->gateway_->resolve(scoped);
	}

	Result<ServicePage> CatalogRuntime::listServices(const _STD string& namespace_name, const _STD string& service_name, int page, int page_size)
	{
		if (!impl_->state_machine_.allowQuery())
		{
			return Result<ServicePage>::failure(impl_->gateFailure());
		}
		const ServiceRegistration registration { impl_->registrationSnapshot() };
		if (!impl_->gateway_)
		{
			return Result<ServicePage>::failure(makeFailure(CatalogError::CATALOG_UNAVAILABLE));
		}
		return impl_->gateway_->listServices(scopeValue(namespace_name, registration.namespace_name), service_name, page, page_size);
	}

	Result<ServiceStatus> CatalogRuntime::getInstanceStatus(
		const _STD string& namespace_name,
		const _STD string& group_name,
		const _STD string& service_id,
		const _STD string& instance_id
	)
	{
		if (!impl_->state_machine_.allowQuery())
		{
			return Result<ServiceStatus>::failure(impl_->gateFailure());
		}
		const ServiceRegistration registration { impl_->registrationSnapshot() };
		if (!impl_->gateway_)
		{
			return Result<ServiceStatus>::failure(makeFailure(CatalogError::CATALOG_UNAVAILABLE));
		}
		return impl_->gateway_->getInstanceStatus(
			scopeValue(namespace_name, registration.namespace_name),
			scopeValue(group_name, registration.group_name),
			service_id,
			instance_id
		);
	}

	Result<_STD string> CatalogRuntime::getLocalIp(void)
	{
		if (!impl_->state_machine_.allowQuery())
		{
			return Result<_STD string>::failure(impl_->gateFailure());
		}
		const ServiceRegistration registration { impl_->registrationSnapshot() };
		if (!impl_->gateway_)
		{
			return Result<_STD string>::failure(makeFailure(CatalogError::CATALOG_UNAVAILABLE));
		}
		return impl_->gateway_->getLocalIp(
			registration.namespace_name,
			registration.group_name,
			registration.service_id,
			impl_->state_machine_.state() == CatalogState::READY
		);
	}

	Result<CatalogServerInfo> CatalogRuntime::getCatalogServerInfo(void)
	{
		if (!impl_->state_machine_.allowQuery())
		{
			return Result<CatalogServerInfo>::failure(impl_->gateFailure());
		}
		if (!impl_->gateway_)
		{
			return Result<CatalogServerInfo>::failure(makeFailure(CatalogError::CATALOG_UNAVAILABLE));
		}
		return impl_->gateway_->getCatalogServerInfo();
	}

	Result<ConfigDocument> CatalogRuntime::putConfig(const ConfigUploadRequest& request)
	{
		const ServiceRegistration registration { impl_->registrationSnapshot() };
		ConfigUploadRequest		  scoped { request };
		scoped.key.namespace_name = scopeValue(request.key.namespace_name, registration.namespace_name);
		scoped.key.group_name	  = scopeValue(request.key.group_name, registration.group_name);
		if (!impl_->state_machine_.allowQuery())
		{
			return Result<ConfigDocument>::failure(impl_->gateFailure());
		}
		if (!impl_->gateway_)
		{
			return Result<ConfigDocument>::failure(makeFailure(CatalogError::CATALOG_UNAVAILABLE));
		}
		Result<ConfigDocument> result { impl_->gateway_->putConfig(scoped) };
		if (!result.isOk())
		{
			return result;
		}
		impl_->config_cache_.remember(scoped.key, result.value());
		impl_->publishConfig(scoped.key, result.value());
		return result;
	}

	Result<_STD vector<ConfigDocument>> CatalogRuntime::getConfig(const ConfigQuery& query)
	{
		if (query.data_ids.empty())
		{
			return Result<_STD vector<ConfigDocument>>::failure(makeFailure(CatalogError::INVALID_ARGUMENT, "data_ids is empty"));
		}
		for (const auto& id : query.data_ids)
		{
			if (id.empty())
			{
				return Result<_STD vector<ConfigDocument>>::
					failure(makeFailure(CatalogError::INVALID_ARGUMENT, "data_ids contains an empty value"));
			}
		}
		const ServiceRegistration registration { impl_->registrationSnapshot() };
		ConfigQuery				  scoped { query };
		scoped.namespace_name = scopeValue(query.namespace_name, registration.namespace_name);
		scoped.group_name	  = scopeValue(query.group_name, registration.group_name);

		if (!impl_->running())
		{
			return Result<_STD vector<ConfigDocument>>::failure(impl_->gateFailure());
		}
		const CatalogState state { impl_->state_machine_.state() };
		if (state == CatalogState::STOPPED || state == CatalogState::STOPPING)
		{
			return Result<_STD vector<ConfigDocument>>::failure(makeFailure(CatalogError::STOPPED));
		}

		if (impl_->state_machine_.allowQuery())
		{
			if (!impl_->gateway_)
			{
				return Result<_STD vector<ConfigDocument>>::failure(makeFailure(CatalogError::CATALOG_UNAVAILABLE));
			}
			Result<_STD vector<ConfigDocument>> remote { impl_->gateway_->getConfigs(scoped) };
			if (remote.isOk())
			{
				for (const auto& document : remote.value())
				{
					impl_->config_cache_.remember(document.key, document);
				}
				return remote;
			}
			const _STD optional<_STD vector<ConfigDocument>> cached { impl_->allCached(scoped) };
			if (cached.has_value())
			{
				return Result<_STD vector<ConfigDocument>>::success(*cached);
			}
			return remote;
		}
		const _STD optional<_STD vector<ConfigDocument>> cached { impl_->allCached(scoped) };
		if (cached.has_value())
		{
			return Result<_STD vector<ConfigDocument>>::success(*cached);
		}
		return Result<_STD vector<ConfigDocument>>::failure(impl_->gateFailure());
	}

	Result<ConfigSubscription> CatalogRuntime::watchConfig(const ConfigKey& key, _STD function<void(const ConfigChangeEvent&)> callback)
	{
		if (key.data_id.empty())
		{
			return Result<ConfigSubscription>::failure(makeFailure(CatalogError::INVALID_ARGUMENT, "data_id is empty"));
		}
		if (!impl_->state_machine_.allowQuery())
		{
			return Result<ConfigSubscription>::failure(impl_->gateFailure());
		}
		const ServiceRegistration registration { impl_->registrationSnapshot() };
		ConfigKey				  scoped {};
		scoped.namespace_name = scopeValue(key.namespace_name, registration.namespace_name);
		scoped.group_name	  = scopeValue(key.group_name, registration.group_name);
		scoped.data_id		  = key.data_id;

		Impl::Watch watch {};
		watch.callback = callback ? _STD move(callback) : _STD function<void(const ConfigChangeEvent&)> {};
		watch.last	   = impl_->config_cache_.current(scoped);
		{
			_STD lock_guard<_STD mutex> lock { impl_->mu_ };
			impl_->watches_[scoped] = watch;
		}
		impl_->next_config_ = Clock::time_point::min();
		impl_->kick();

		ConfigSubscription subscription { [impl = impl_.get(), scoped]()
										  {
											  _STD lock_guard<_STD mutex> lock { impl->mu_ };
											  impl->watches_.erase(scoped);
										  } };
		return Result<ConfigSubscription>::success(_STD move(subscription));
	}

	Result<void> CatalogRuntime::updateLogPaths(const _STD vector<LogPath>& paths)
	{
		for (const auto& path : paths)
		{
			if (path.path.empty() || path.path.front() != '/')
			{
				return Result<void>::failure(makeFailure(CatalogError::INVALID_ARGUMENT, "log path must be absolute"));
			}
		}
		if (!impl_->running())
		{
			return Result<void>::failure(makeFailure(CatalogError::NOT_STARTED));
		}
		{
			_STD lock_guard<_STD mutex> lock { impl_->mu_ };
			impl_->registration_.log_paths = paths;
			impl_->instance_id_.clear();
		}
		impl_->retry_delay_		  = _STD_CHRONO milliseconds { 1000 };
		impl_->next_registration_ = Clock::time_point::min();
		if (impl_->state_machine_.state() == CatalogState::READY)
		{
			impl_->transition(CatalogState::REGISTERING, CatalogEventType::STATE_CHANGED, "log paths changed");
		}
		impl_->kick();
		return Result<void>::success();
	}

	Result<void> CatalogRuntime::updateRegistration(const ServiceRegistration& registration)
	{
		if (!impl_->running())
		{
			return Result<void>::failure(makeFailure(CatalogError::NOT_STARTED));
		}
		if (registration.service_id.empty() && registration.service_name.empty())
		{
			return Result<void>::failure(makeFailure(CatalogError::INVALID_ARGUMENT));
		}
		{
			_STD lock_guard<_STD mutex> lock { impl_->mu_ };
			impl_->registration_				= registration;
			impl_->registration_.namespace_name = scopeValue(registration.namespace_name, "public");
			impl_->registration_.group_name		= scopeValue(registration.group_name, "DEFAULT_GROUP");
			impl_->registration_requested_		= true;
			impl_->instance_id_.clear();
		}
		impl_->retry_delay_		  = _STD_CHRONO milliseconds { 1000 };
		impl_->next_registration_ = Clock::time_point::min();
		if (impl_->state_machine_.state() == CatalogState::READY)
		{
			impl_->transition(CatalogState::REGISTERING, CatalogEventType::STATE_CHANGED, "registration updated");
		}
		impl_->kick();
		return Result<void>::success();
	}
} // namespace plane::catalog

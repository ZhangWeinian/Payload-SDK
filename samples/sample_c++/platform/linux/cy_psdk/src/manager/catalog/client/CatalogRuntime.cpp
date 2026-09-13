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

#include <fmt/format.h>

#include "manager/catalog/client/CatalogError.h"
#include "manager/catalog/client/CatalogFailure.h"
#include "manager/catalog/client/internal/codec/JsonCodec.h"
#include "manager/catalog/client/internal/discovery/DiscoveryClient.h"
#include "manager/catalog/client/internal/discovery/UdpDiscoveryClient.h"
#include "manager/catalog/client/internal/service/ServiceGateway.h"
#include "manager/catalog/client/internal/state/AvailabilityTracker.h"
#include "manager/catalog/client/internal/state/ConfigCache.h"
#include "manager/catalog/client/internal/state/RuntimeStateMachine.h"
#include "manager/catalog/client/internal/transport/HttpTransport.h"

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
        using Clock = ::std::chrono::steady_clock;

        constexpr ::std::string scopeValue(const ::std::string& value, const ::std::string& fallback)
        {
            return value.empty() ? fallback : value;
        }

        constexpr ::std::chrono::milliseconds positiveInterval(::std::chrono::milliseconds value, ::std::chrono::milliseconds fallback)
        {
            return value.count() > 0 ? value : fallback;
        }

        [[nodiscard]] ::std::string catalogUrlOf(const CatalogEndpoint& endpoint)
        {
            return ::fmt::format("http://{}:{}", endpoint.ip, endpoint.http_port == 0 ? 8081 : endpoint.http_port);
        }
    } // namespace

    // 实现体: 持有全部运行时状态与线程
    struct CatalogRuntime::Impl
    {
        struct Watch
        {
            ::std::function<void(const ConfigChangeEvent&)> callback {};
            ::std::optional<ConfigDocument>                 last {};
        };

        CatalogRuntimeOptions              options_ {};
        DiscoveryConfig                    discovery_config_ {};
        ::std::unique_ptr<DiscoveryClient> discovery_ {};

        ::std::unique_ptr<ServiceGateway>  gateway_ {};

        // 注入的 HTTP 传输 (为空时使用默认 CppHttpTransport; start 成功后移交给 ServiceGateway)
        ::std::unique_ptr<internal::HttpTransport> http_transport_ {};

        // start/stop 串行化 (对齐 java synchronized)
        ::std::mutex lifecycle_mu_ {};

        // 状态 (mu_ 保护共享可变字段)
        mutable ::std::mutex             mu_ {};
        RuntimeStateMachine              state_machine_ {};
        AvailabilityTracker              availability_ { 3 };
        ConfigCache                      config_cache_ {};
        ::std::optional<CatalogEndpoint> endpoint_ {};
        ::std::string                    instance_id_ {};
        ServiceRegistration              registration_ {};
        ::std::optional<ServiceStatus>   latest_status_ {};
        ::std::map<ConfigKey, Watch>     watches_ {};

        // 跨线程标志 (原子; 业务线程与控制线程并发访问)
        ::std::atomic<bool> status_dirty_ { false };
        ::std::atomic<bool> registration_requested_ { false };
        ::std::atomic<bool> ever_started_ { false };

        // 调度 (原子: 业务线程可请求"立即执行", 时间戳语义由控制线程维护)
        ::std::atomic<Clock::time_point>           next_registration_ { Clock::time_point::min() };
        ::std::atomic<Clock::time_point>           next_heartbeat_ { Clock::time_point::min() };
        ::std::atomic<Clock::time_point>           next_status_report_ { Clock::time_point::min() };
        ::std::atomic<Clock::time_point>           next_config_ { Clock::time_point::min() };
        ::std::atomic<Clock::time_point>           next_discovery_ { Clock::time_point::min() };
        ::std::atomic<::std::chrono::milliseconds> retry_delay_ { ::std::chrono::milliseconds { 1000 } };

        // 控制线程
        ::std::atomic<bool>       quit_ { false };
        ::std::atomic<bool>       discovery_cancelled_ { false };
        ::std::atomic<bool>       wake_requested_ { false };
        ::std::thread             control_thread_ {};
        ::std::mutex              wake_mu_ {};
        ::std::condition_variable wake_cv_ {};

        // 回调线程
        ::std::atomic<bool>                   cb_quit_ { false };
        ::std::thread                         cb_thread_ {};
        ::std::mutex                          cb_mu_ {};
        ::std::condition_variable             cb_cv_ {};
        ::std::deque<::std::function<void()>> cb_queue_ {};

        explicit Impl(
            CatalogRuntimeOptions options,
            DiscoveryConfig       discovery_config
        ): options_(::std::move(options)),
           discovery_config_(::std::move(discovery_config)),
           availability_(this->options_.unavailable_failure_threshold > 0 ? this->options_.unavailable_failure_threshold : 3)
        {}

        [[nodiscard]] bool running(void) const
        {
            if (!this->ever_started_.load(::std::memory_order_acquire) || this->quit_.load(::std::memory_order_acquire))
            {
                return false;
            }
            const CatalogState state { this->state_machine_.state() };
            return state != CatalogState::STOPPED && state != CatalogState::STOPPING;
        }

        [[nodiscard]] ::std::chrono::milliseconds heartbeatInterval(void) const
        {
            return positiveInterval(this->options_.heartbeat_interval, ::std::chrono::milliseconds { 3000 });
        }

        [[nodiscard]] ::std::chrono::milliseconds configCheckInterval(void) const
        {
            return positiveInterval(this->options_.config_check_interval, ::std::chrono::milliseconds { 5000 });
        }

        [[nodiscard]] ::std::chrono::milliseconds httpTimeout(void) const
        {
            return positiveInterval(this->options_.http_timeout, ::std::chrono::milliseconds { 10'000 });
        }

        [[nodiscard]] CatalogFailure gateFailure(void) const
        {
            if (!this->ever_started_.load(::std::memory_order_acquire))
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
                    return makeFailure(CatalogError::CATALOG_UNAVAILABLE, ::std::string { defaultErrorMessage(CatalogError::NOT_STARTED) });
                default:
                    return makeFailure(CatalogError::CATALOG_UNAVAILABLE);
            }
        }

        // 回调投递
        void dispatch(::std::function<void()> task)
        {
            {
                ::std::lock_guard<::std::mutex> lock { this->cb_mu_ };
                this->cb_queue_.push_back(::std::move(task));
            }
            this->cb_cv_.notify_one();
        }

        void postEvent(
            CatalogEventType                 type,
            CatalogState                     previous,
            CatalogState                     current,
            const ::std::string&             message,
            ::std::optional<CatalogEndpoint> event_endpoint,
            ::std::vector<CatalogEndpoint>   conflicts
        )
        {
            if (!this->options_.event_callback)
            {
                return;
            }
            CatalogEvent event {};
            event.type               = type;
            event.previous_state     = previous;
            event.current_state      = current;
            event.message            = message;
            event.endpoint           = ::std::move(event_endpoint);
            event.conflict_endpoints = ::std::move(conflicts);
            this->dispatch(
                [callback = this->options_.event_callback, event = ::std::move(event)]() mutable
                {
                    callback(event);
                }
            );
        }

        void transition(CatalogState next, CatalogEventType type, const ::std::string& message)
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
            ::std::optional<CatalogEndpoint> ep {};
            {
                ::std::lock_guard<::std::mutex> lock { this->mu_ };
                ep = this->endpoint_;
            }
            this->postEvent(type, previous, next, message, ::std::move(ep), {});
        }

        void kick(void)
        {
            // 置位唤醒标志并通知: 控制线程 wait_for 谓词会消费该标志立即执行 tick
            this->wake_requested_.store(true, ::std::memory_order_release);
            this->wake_cv_.notify_one();
        }

        // 控制线程
        void runControl(void)
        {
            for (;;)
            {
                {
                    ::std::unique_lock<::std::mutex> lock { this->wake_mu_ };
                    this->wake_cv_.wait_for(
                        lock,
                        ::std::chrono::milliseconds { 10 },
                        [this]
                        {
                            return this->quit_.load(::std::memory_order_acquire) ||
                                   this->wake_requested_.exchange(false, ::std::memory_order_acq_rel);
                        }
                    );
                }
                if (this->quit_.load(::std::memory_order_acquire))
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
                ::std::function<void()> task {};
                {
                    ::std::unique_lock<::std::mutex> lock { this->cb_mu_ };
                    this->cb_cv_.wait(
                        lock,
                        [this]
                        {
                            return this->cb_quit_.load(::std::memory_order_acquire) || !this->cb_queue_.empty();
                        }
                    );
                    if (this->cb_queue_.empty())
                    {
                        if (this->cb_quit_.load(::std::memory_order_acquire))
                        {
                            break;
                        }
                        continue;
                    }
                    task = ::std::move(this->cb_queue_.front());
                    this->cb_queue_.pop_front();
                }
                try
                {
                    task();
                }
                catch (...) // NOLINT(bugprone-empty-catch)
                {
                    // 回调异常被吞掉, 不终止运行时
                }
            }
        }

        // tick 驱动 (对齐 java tick/tickSafely)
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

            if (now >= this->next_discovery_.load(::std::memory_order_acquire))
            {
                this->refreshDiscovery();
                this->next_discovery_.store(
                    Clock::now() + (this->state_machine_.state() == CatalogState::READY ? this->discovery_config_.ready_probe_interval
                                                                                        : this->discovery_config_.response_window),
                    ::std::memory_order_release
                );
            }
            if (this->state_machine_.allowRegister() && this->registration_requested_.load(::std::memory_order_acquire) &&
                !this->hasInstanceId() && now >= this->next_registration_.load(::std::memory_order_acquire))
            {
                this->registerOnce();
            }
            if (this->state_machine_.allowStatusReport() && this->hasInstanceId() &&
                now >= this->next_heartbeat_.load(::std::memory_order_acquire))
            {
                this->heartbeatOnce();
            }
            if (this->state_machine_.allowStatusReport() && this->status_dirty_.load(::std::memory_order_acquire) &&
                now >= this->next_status_report_.load(::std::memory_order_acquire))
            {
                this->reportLatestStatus();
            }
            if (this->hasWatches() && this->state_machine_.allowConfigRefresh() && now >= this->next_config_.load(::std::memory_order_acquire))
            {
                this->pollConfigs();
            }
        }

        // 注册 / 心跳 / 状态 / 配置
        void registerOnce(void)
        {
            if (!this->gateway_)
            {
                return;
            }
            ServiceRegistration   registration { this->registrationSnapshot() };
            Result<::std::string> result { this->gateway_->registerInstance(registration) };
            if (result.has_value() && !result.value().empty())
            {
                {
                    ::std::lock_guard<::std::mutex> lock { this->mu_ };
                    this->instance_id_ = result.value();
                }
                ::std::optional<CatalogEndpoint> ep { this->endpointSnapshot() };
                if (ep.has_value())
                {
                    this->discovery_->saveSuccessfulIp(ep->ip);
                }
                this->retry_delay_.store(::std::chrono::milliseconds { 1000 }, ::std::memory_order_release);
                this->availability_.success();
                this->next_heartbeat_.store(Clock::now() + this->heartbeatInterval(), ::std::memory_order_release);
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
                    this->retry_delay_
                        .store(::std::min(this->retry_delay_.load() * 2, ::std::chrono::milliseconds { 2000 }), ::std::memory_order_release);
                    this->next_registration_.store(Clock::now() + this->retry_delay_.load(), ::std::memory_order_release);
                }
                else
                {
                    this->next_registration_.store(Clock::now() + ::std::chrono::seconds(2), ::std::memory_order_release);
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
            const ::std::string id { this->instanceId() };
            Result<void>        result { this->gateway_->heartbeat(registration, id) };
            this->next_heartbeat_.store(Clock::now() + this->heartbeatInterval(), ::std::memory_order_release);
            if (result.has_value())
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
            ::std::optional<ServiceStatus> snapshot {};
            {
                ::std::lock_guard<::std::mutex> lock { this->mu_ };
                snapshot = this->latest_status_;
                this->status_dirty_.store(false, ::std::memory_order_release);
            }
            const ::std::string id { this->instanceId() };
            if (!snapshot.has_value() || id.empty())
            {
                {
                    ::std::lock_guard<::std::mutex> lock { this->mu_ };
                    this->status_dirty_.store(true, ::std::memory_order_release);
                }
                return;
            }
            ServiceRegistration registration { this->registrationSnapshot() };
            Result<void>        result { this->gateway_->reportStatus(registration, id, *snapshot) };
            if (result.has_value())
            {
                this->availability_.success();
                this->next_status_report_.store(Clock::time_point::min(), ::std::memory_order_release);
                return;
            }
            {
                ::std::lock_guard<::std::mutex> lock { this->mu_ };
                this->status_dirty_.store(true, ::std::memory_order_release);
            }
            this->next_status_report_.store(Clock::now() + ::std::chrono::seconds(2), ::std::memory_order_release);
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
                this->next_status_report_.store(Clock::time_point::min(), ::std::memory_order_release); // 重注册成功后立即补报
            }
            else
            {
                this->noteCatalogFailure(error);
            }
        }

        void resetInstanceForReregister(const ::std::string& message)
        {
            {
                ::std::lock_guard<::std::mutex> lock { this->mu_ };
                this->instance_id_.clear();
            }
            this->retry_delay_.store(::std::chrono::milliseconds { 1000 }, ::std::memory_order_release);
            this->next_registration_.store(Clock::time_point::min(), ::std::memory_order_release);
            this->transition(CatalogState::REGISTERING, CatalogEventType::STATE_CHANGED, message);
        }

        void pollConfigs(void)
        {
            ::std::vector<::std::pair<ConfigKey, Watch>> snapshot {};
            {
                ::std::lock_guard<::std::mutex> lock { this->mu_ };
                for (const auto& [key, watch] : this->watches_)
                {
                    snapshot.emplace_back(key, watch);
                }
            }
            for (auto& [key, watch] : snapshot)
            {
                Result<ConfigDocument> result { this->gateway_->getConfig(key) };
                if (!result.has_value())
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
                const bool     changed { !watch.last.has_value() || watch.last->version != current.version ||
                                         watch.last->content != current.content };
                if (!changed)
                {
                    continue;
                }
                ConfigDocument previous { watch.last.value_or(ConfigDocument {}) };
                {
                    ::std::lock_guard<::std::mutex> lock { this->mu_ };
                    watch.last               = current;
                    this->watches_[key].last = current;
                }
                this->config_cache_.remember(key, current);
                const bool        initial_load { previous.version.empty() && previous.content.empty() };
                ConfigChangeEvent change {};
                change.previous     = previous;
                change.current      = current;
                change.initial_load = initial_load;
                this->dispatch(
                    [callback = watch.callback, change = ::std::move(change)]() mutable
                    {
                        callback(change);
                    }
                );
            }
            this->next_config_.store(Clock::now() + this->configCheckInterval(), ::std::memory_order_release);
        }

        void publishConfig(const ConfigKey& key, const ConfigDocument& current)
        {
            Watch watch {};
            {
                ::std::lock_guard<::std::mutex> lock { this->mu_ };
                const auto                      it { this->watches_.find(key) };
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
                ::std::lock_guard<::std::mutex> lock { this->mu_ };
                this->watches_[key].last = current;
            }
            this->config_cache_.remember(key, current);
            ConfigChangeEvent change {};
            change.previous     = previous;
            change.current      = current;
            change.initial_load = previous.version.empty() && previous.content.empty();
            this->dispatch(
                [callback = watch.callback, change = ::std::move(change)]() mutable
                {
                    callback(change);
                }
            );
        }

        // 周期重发现
        void refreshDiscovery(void)
        {
            const CatalogState    previous { this->state_machine_.state() };
            const DiscoveryReport report { this->discovery_->discover(this->discovery_config_, this->discovery_cancelled_) };

            if (report.multiple_instances || report.endpoints.size() > 1)
            {
                ::std::optional<CatalogEndpoint> old {};
                {
                    ::std::lock_guard<::std::mutex> lock { this->mu_ };
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
                    ::std::lock_guard<::std::mutex> lock { this->mu_ };
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
            const ::std::string   url { catalogUrlOf(discovered) };
            const bool            unchanged { previous != CatalogState::CONFLICT && previous != CatalogState::UNAVAILABLE &&
                                              previous != CatalogState::DISCOVERING && this->gateway_ && this->gateway_->catalogUrl() == url };

            {
                ::std::lock_guard<::std::mutex> lock { this->mu_ };
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
                    ::std::lock_guard<::std::mutex> lock { this->mu_ };
                    requested = this->registration_requested_.load(::std::memory_order_acquire);
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
                this->next_registration_.store(Clock::time_point::min(), ::std::memory_order_release);
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
                ::std::lock_guard<::std::mutex> lock { this->mu_ };
                this->instance_id_.clear();
            }
            this->transition(CatalogState::UNAVAILABLE, CatalogEventType::CATALOG_LOST, error.message);
            this->next_discovery_.store(Clock::time_point::min(), ::std::memory_order_release);
        }

        // 快照访问
        [[nodiscard]] bool hasInstanceId(void) const
        {
            ::std::lock_guard<::std::mutex> lock { this->mu_ };
            return !this->instance_id_.empty();
        }

        [[nodiscard]] bool hasWatches(void) const
        {
            ::std::lock_guard<::std::mutex> lock { this->mu_ };
            return !this->watches_.empty();
        }

        [[nodiscard]] ServiceRegistration registrationSnapshot(void) const
        {
            ::std::lock_guard<::std::mutex> lock { this->mu_ };
            return this->registration_;
        }

        [[nodiscard]] ::std::optional<CatalogEndpoint> endpointSnapshot(void) const
        {
            ::std::lock_guard<::std::mutex> lock { this->mu_ };
            return this->endpoint_;
        }

        [[nodiscard]] ::std::string instanceId(void) const
        {
            ::std::lock_guard<::std::mutex> lock { this->mu_ };
            return this->instance_id_;
        }

        // 配置/查询辅助
        [[nodiscard]] ::std::optional<::std::vector<ConfigDocument>> allCached(const ConfigQuery& query)
        {
            ::std::vector<ConfigDocument> documents {};
            for (const auto& data_id : query.data_ids)
            {
                const ConfigKey                       key { query.namespace_name, query.group_name, data_id };
                const ::std::optional<ConfigDocument> cached { this->config_cache_.cached(key) };
                if (!cached.has_value())
                {
                    return ::std::nullopt;
                }
                documents.push_back(*cached);
            }
            return documents;
        }
    };

    // ==================== CatalogRuntime 公开方法 ====================

    // 默认实现: 不注入传输/发现实现 (委托给显式注入版本)
    CatalogRuntime::CatalogRuntime(
        CatalogRuntimeOptions options,
        DiscoveryConfig       discovery_config
    ): CatalogRuntime(::std::move(options), ::std::move(discovery_config), nullptr, nullptr)
    {}

    CatalogRuntime::CatalogRuntime(
        CatalogRuntimeOptions                        options,
        DiscoveryConfig                              discovery_config,
        ::std::unique_ptr<internal::HttpTransport>   http_transport,
        ::std::unique_ptr<internal::DiscoveryClient> discovery_client
    ): impl_(::std::make_unique<Impl>(::std::move(options), ::std::move(discovery_config)))
    {
        // 归一化注册信息 (namespace/group 默认 public/DEFAULT_GROUP, version 去除首尾空白)
        {
            ::std::lock_guard<::std::mutex> lock { impl_->mu_ };
            impl_->registration_                = impl_->options_.registration;
            impl_->registration_.namespace_name = scopeValue(impl_->registration_.namespace_name, "public");
            impl_->registration_.group_name     = scopeValue(impl_->registration_.group_name, "DEFAULT_GROUP");
            Result<::std::string> version { internal::JsonCodec::normalizeVersion(impl_->registration_.version) };
            if (version.has_value())
            {
                impl_->registration_.version = version.value();
            }
        }
        impl_->discovery_      = discovery_client ? ::std::move(discovery_client) : ::std::make_unique<internal::UdpDiscoveryClient>();
        impl_->http_transport_ = ::std::move(http_transport); // start 成功后移交给 ServiceGateway; 为空时使用默认实现
    }

    CatalogRuntime::~CatalogRuntime(void)
    {
        try
        {
            (void)this->stop(::std::chrono::seconds(2));
        }
        catch (...) // NOLINT(bugprone-empty-catch)
        {
            // 析构中 best-effort 停止; 失败不影响析构 (不得抛)
        }
    }

    Result<void> CatalogRuntime::start(void)
    {
        ::std::lock_guard<::std::mutex> lifecycle_lock { impl_->lifecycle_mu_ }; // start/stop 串行化 (对齐 java synchronized)
        if (impl_->ever_started_.load(::std::memory_order_acquire))
        {
            return ::std::unexpected(makeFailure(CatalogError::ALREADY_STARTED));
        }

        // 注册参数校验
        ServiceRegistration registration { impl_->registrationSnapshot() };
        if (registration.service_id.empty())
        {
            return ::std::unexpected(makeFailure(CatalogError::INVALID_ARGUMENT, "service_id is empty"));
        }
        if (registration.service_name.empty())
        {
            return ::std::unexpected(makeFailure(CatalogError::INVALID_ARGUMENT, "service_name is empty"));
        }
        Result<::std::string> version { internal::JsonCodec::normalizeVersion(registration.version) };
        if (!version.has_value())
        {
            return ::std::unexpected(version.error());
        }

        impl_->discovery_cancelled_.store(false, ::std::memory_order_release);
        impl_->state_machine_.set(CatalogState::DISCOVERING);
        const DiscoveryReport report { impl_->discovery_->discover(impl_->discovery_config_, impl_->discovery_cancelled_) };

        if (report.multiple_instances || report.endpoints.size() > 1)
        {
            impl_->state_machine_.set(CatalogState::STOPPED);
            return ::std::unexpected(makeFailure(CatalogError::CATALOG_CONFLICT, "multiple catalog instances"));
        }
        if (report.endpoints.empty())
        {
            impl_->state_machine_.set(CatalogState::STOPPED);
            const CatalogError code { report.status == DiscoveryStatus::INVALID_ARGUMENT ? CatalogError::INVALID_ARGUMENT
                                                                                         : CatalogError::DISCOVERY_TIMEOUT };
            return ::std::unexpected(makeFailure(code, report.error));
        }

        const CatalogEndpoint discovered { report.endpoints.front() };
        {
            ::std::lock_guard<::std::mutex> lock { impl_->mu_ };
            impl_->endpoint_ = discovered;
        }
        impl_->gateway_ =
            ::std::make_unique<ServiceGateway>(catalogUrlOf(discovered), ::std::move(impl_->http_transport_), impl_->httpTimeout());
        impl_->gateway_->setRemoteAllowed(true);
        impl_->state_machine_.set(CatalogState::DISCOVERED);

        // 先标记已启动再创建线程: 若线程创建抛异常, 后续 start 返回 ALREADY_STARTED, 不会二次赋值 ::std::thread (terminate)
        impl_->ever_started_.store(true, ::std::memory_order_release);
        impl_->cb_quit_.store(false, ::std::memory_order_release);
        impl_->cb_thread_ = ::std::thread(&Impl::runCallbacks, impl_.get());
        impl_->quit_.store(false, ::std::memory_order_release);
        impl_->control_thread_ = ::std::thread(&Impl::runControl, impl_.get());

        const Clock::time_point now { Clock::now() };
        impl_->next_config_.store(now, ::std::memory_order_release);
        impl_->next_discovery_.store(now + impl_->discovery_config_.ready_probe_interval, ::std::memory_order_release);
        impl_->kick();
        return {};
    }

    Result<void> CatalogRuntime::registerServiceInstance(void)
    {
        if (!impl_->running())
        {
            return ::std::unexpected(makeFailure(CatalogError::NOT_STARTED));
        }
        const CatalogState state { impl_->state_machine_.state() };
        if (state == CatalogState::CONFLICT)
        {
            return ::std::unexpected(makeFailure(CatalogError::CATALOG_CONFLICT));
        }
        if (state == CatalogState::UNAVAILABLE)
        {
            return ::std::unexpected(makeFailure(CatalogError::CATALOG_UNAVAILABLE));
        }
        if (state == CatalogState::STOPPING || state == CatalogState::STOPPED)
        {
            return ::std::unexpected(makeFailure(CatalogError::STOPPED));
        }
        impl_->registration_requested_.store(true, ::std::memory_order_release);
        if (state == CatalogState::DISCOVERED)
        {
            impl_->transition(CatalogState::REGISTERING, CatalogEventType::STATE_CHANGED, "registration requested");
        }
        impl_->next_registration_.store(Clock::time_point::min(), ::std::memory_order_release);
        impl_->kick();
        return {};
    }

    Result<void> CatalogRuntime::stop(::std::chrono::milliseconds timeout)
    {
        ::std::lock_guard<::std::mutex> lifecycle_lock { impl_->lifecycle_mu_ }; // start/stop 串行化 (对齐 java synchronized)
        {
            ::std::lock_guard<::std::mutex> lock { impl_->mu_ };
            if (!impl_->ever_started_.load(::std::memory_order_acquire) || impl_->state_machine_.state() == CatalogState::STOPPED)
            {
                return ::std::unexpected(makeFailure(CatalogError::NOT_STARTED));
            }
        }
        impl_->state_machine_.set(CatalogState::STOPPING);
        impl_->discovery_cancelled_.store(true, ::std::memory_order_release);
        if (impl_->gateway_)
        {
            impl_->gateway_->setStopping(true);
            impl_->gateway_->setRemoteAllowed(false);
        }
        impl_->quit_.store(true, ::std::memory_order_release);
        impl_->wake_cv_.notify_all();
        if (impl_->control_thread_.joinable())
        {
            impl_->control_thread_.join();
        }

        {
            ::std::lock_guard<::std::mutex> lock { impl_->cb_mu_ };
            impl_->cb_quit_.store(true, ::std::memory_order_release);
        }
        impl_->cb_cv_.notify_all();
        if (impl_->cb_thread_.joinable())
        {
            impl_->cb_thread_.join();
        }

        impl_->state_machine_.set(CatalogState::STOPPED);

        // best-effort 注销 (需要临时放开网关)
        const ::std::string instance_id { impl_->instanceId() };
        if (!instance_id.empty() && impl_->gateway_)
        {
            impl_->gateway_->setStopping(false);
            impl_->gateway_->setConflict(false);
            impl_->gateway_->setRemoteAllowed(true);
            impl_->gateway_->setTimeout(positiveInterval(timeout, ::std::chrono::seconds(2)));
            (void)impl_->gateway_->deleteInstance(impl_->registrationSnapshot(), instance_id);
        }
        return {};
    }

    CatalogState CatalogRuntime::state(void) const
    {
        return impl_->state_machine_.state();
    }

    ::std::optional<CatalogEndpoint> CatalogRuntime::catalogEndpoint(void) const
    {
        return impl_->endpointSnapshot();
    }

    ::std::string CatalogRuntime::instanceId(void) const
    {
        return impl_->instanceId();
    }

    Result<void> CatalogRuntime::updateStatus(const ServiceStatus& status)
    {
        if (!impl_->running())
        {
            return ::std::unexpected(makeFailure(CatalogError::NOT_STARTED));
        }
        {
            ::std::lock_guard<::std::mutex> lock { impl_->mu_ };
            impl_->latest_status_ = status;
        }
        impl_->status_dirty_.store(true, ::std::memory_order_release);
        impl_->next_status_report_.store(Clock::time_point::min(), ::std::memory_order_release);
        impl_->kick();
        return {};
    }

    Result<ResolvedService> CatalogRuntime::resolveService(const ServiceQuery& query)
    {
        if (!impl_->state_machine_.allowQuery())
        {
            return ::std::unexpected(impl_->gateFailure());
        }
        const ServiceRegistration registration { impl_->registrationSnapshot() };
        ServiceQuery              scoped {};
        scoped.namespace_name = scopeValue(query.namespace_name, registration.namespace_name);
        scoped.group_name     = scopeValue(query.group_name, registration.group_name);
        scoped.service_id     = query.service_id;
        scoped.service_name   = query.service_name;
        if (!impl_->gateway_)
        {
            return ::std::unexpected(makeFailure(CatalogError::CATALOG_UNAVAILABLE));
        }
        return impl_->gateway_->resolve(scoped);
    }

    Result<ServicePage>
        CatalogRuntime::listServices(const ::std::string& namespace_name, const ::std::string& service_name, int page, int page_size)
    {
        if (!impl_->state_machine_.allowQuery())
        {
            return ::std::unexpected(impl_->gateFailure());
        }
        const ServiceRegistration registration { impl_->registrationSnapshot() };
        if (!impl_->gateway_)
        {
            return ::std::unexpected(makeFailure(CatalogError::CATALOG_UNAVAILABLE));
        }
        return impl_->gateway_->listServices(scopeValue(namespace_name, registration.namespace_name), service_name, page, page_size);
    }

    Result<ServiceStatus> CatalogRuntime::getInstanceStatus(
        const ::std::string& namespace_name,
        const ::std::string& group_name,
        const ::std::string& service_id,
        const ::std::string& instance_id
    )
    {
        if (!impl_->state_machine_.allowQuery())
        {
            return ::std::unexpected(impl_->gateFailure());
        }
        const ServiceRegistration registration { impl_->registrationSnapshot() };
        if (!impl_->gateway_)
        {
            return ::std::unexpected(makeFailure(CatalogError::CATALOG_UNAVAILABLE));
        }
        return impl_->gateway_->getInstanceStatus(
            scopeValue(namespace_name, registration.namespace_name),
            scopeValue(group_name, registration.group_name),
            service_id,
            instance_id
        );
    }

    Result<::std::string> CatalogRuntime::getLocalIp(void)
    {
        if (!impl_->state_machine_.allowQuery())
        {
            return ::std::unexpected(impl_->gateFailure());
        }
        const ServiceRegistration registration { impl_->registrationSnapshot() };
        if (!impl_->gateway_)
        {
            return ::std::unexpected(makeFailure(CatalogError::CATALOG_UNAVAILABLE));
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
            return ::std::unexpected(impl_->gateFailure());
        }
        if (!impl_->gateway_)
        {
            return ::std::unexpected(makeFailure(CatalogError::CATALOG_UNAVAILABLE));
        }
        return impl_->gateway_->getCatalogServerInfo();
    }

    Result<ConfigDocument> CatalogRuntime::putConfig(const ConfigUploadRequest& request)
    {
        const ServiceRegistration registration { impl_->registrationSnapshot() };
        ConfigUploadRequest       scoped { request };
        scoped.key.namespace_name = scopeValue(request.key.namespace_name, registration.namespace_name);
        scoped.key.group_name     = scopeValue(request.key.group_name, registration.group_name);
        if (!impl_->state_machine_.allowQuery())
        {
            return ::std::unexpected(impl_->gateFailure());
        }
        if (!impl_->gateway_)
        {
            return ::std::unexpected(makeFailure(CatalogError::CATALOG_UNAVAILABLE));
        }
        Result<ConfigDocument> result { impl_->gateway_->putConfig(scoped) };
        if (!result.has_value())
        {
            return result;
        }
        impl_->config_cache_.remember(scoped.key, result.value());
        impl_->publishConfig(scoped.key, result.value());
        return result;
    }

    Result<::std::vector<ConfigDocument>> CatalogRuntime::getConfig(const ConfigQuery& query)
    {
        if (query.data_ids.empty())
        {
            return ::std::unexpected(makeFailure(CatalogError::INVALID_ARGUMENT, "data_ids is empty"));
        }
        for (const auto& id : query.data_ids)
        {
            if (id.empty())
            {
                return ::std::unexpected(makeFailure(CatalogError::INVALID_ARGUMENT, "data_ids contains an empty value"));
            }
        }
        const ServiceRegistration registration { impl_->registrationSnapshot() };
        ConfigQuery               scoped { query };
        scoped.namespace_name = scopeValue(query.namespace_name, registration.namespace_name);
        scoped.group_name     = scopeValue(query.group_name, registration.group_name);

        if (!impl_->running())
        {
            return ::std::unexpected(impl_->gateFailure());
        }
        const CatalogState state { impl_->state_machine_.state() };
        if (state == CatalogState::STOPPED || state == CatalogState::STOPPING)
        {
            return ::std::unexpected(makeFailure(CatalogError::STOPPED));
        }

        if (impl_->state_machine_.allowQuery())
        {
            if (!impl_->gateway_)
            {
                return ::std::unexpected(makeFailure(CatalogError::CATALOG_UNAVAILABLE));
            }
            Result<::std::vector<ConfigDocument>> remote { impl_->gateway_->getConfigs(scoped) };
            if (remote.has_value())
            {
                for (const auto& document : remote.value())
                {
                    impl_->config_cache_.remember(document.key, document);
                }
                return remote;
            }
            const ::std::optional<::std::vector<ConfigDocument>> cached { impl_->allCached(scoped) };
            if (cached.has_value())
            {
                return *cached;
            }
            return remote;
        }
        const ::std::optional<::std::vector<ConfigDocument>> cached { impl_->allCached(scoped) };
        if (cached.has_value())
        {
            return *cached;
        }
        return ::std::unexpected(impl_->gateFailure());
    }

    Result<ConfigSubscription> CatalogRuntime::watchConfig(const ConfigKey& key, ::std::function<void(const ConfigChangeEvent&)> callback)
    {
        if (key.data_id.empty())
        {
            return ::std::unexpected(makeFailure(CatalogError::INVALID_ARGUMENT, "data_id is empty"));
        }
        if (!impl_->state_machine_.allowQuery())
        {
            return ::std::unexpected(impl_->gateFailure());
        }
        const ServiceRegistration registration { impl_->registrationSnapshot() };
        ConfigKey                 scoped {};
        scoped.namespace_name = scopeValue(key.namespace_name, registration.namespace_name);
        scoped.group_name     = scopeValue(key.group_name, registration.group_name);
        scoped.data_id        = key.data_id;

        Impl::Watch watch {};
        watch.callback = callback ? ::std::move(callback) : ::std::function<void(const ConfigChangeEvent&)> {};
        watch.last     = impl_->config_cache_.current(scoped);
        {
            ::std::lock_guard<::std::mutex> lock { impl_->mu_ };
            impl_->watches_[scoped] = watch;
        }
        impl_->next_config_.store(Clock::time_point::min(), ::std::memory_order_release);
        impl_->kick();

        ConfigSubscription subscription { [impl = impl_.get(), scoped]()
                                          {
                                              ::std::lock_guard<::std::mutex> lock { impl->mu_ };
                                              impl->watches_.erase(scoped);
                                          } };
        return subscription;
    }

    Result<void> CatalogRuntime::updateLogPaths(const ::std::vector<LogPath>& paths)
    {
        for (const auto& path : paths)
        {
            if (path.path.empty() || path.path.front() != '/')
            {
                return ::std::unexpected(makeFailure(CatalogError::INVALID_ARGUMENT, "log path must be absolute"));
            }
        }
        if (!impl_->running())
        {
            return ::std::unexpected(makeFailure(CatalogError::NOT_STARTED));
        }
        {
            ::std::lock_guard<::std::mutex> lock { impl_->mu_ };
            impl_->registration_.log_paths = paths;
            impl_->instance_id_.clear();
        }
        impl_->retry_delay_.store(::std::chrono::milliseconds { 1000 }, ::std::memory_order_release);
        impl_->next_registration_.store(Clock::time_point::min(), ::std::memory_order_release);
        if (impl_->state_machine_.state() == CatalogState::READY)
        {
            impl_->transition(CatalogState::REGISTERING, CatalogEventType::STATE_CHANGED, "log paths changed");
        }
        impl_->kick();
        return {};
    }

    Result<void> CatalogRuntime::updateRegistration(const ServiceRegistration& registration)
    {
        if (!impl_->running())
        {
            return ::std::unexpected(makeFailure(CatalogError::NOT_STARTED));
        }
        if (registration.service_id.empty() && registration.service_name.empty())
        {
            return ::std::unexpected(makeFailure(CatalogError::INVALID_ARGUMENT));
        }
        {
            ::std::lock_guard<::std::mutex> lock { impl_->mu_ };
            impl_->registration_                = registration;
            impl_->registration_.namespace_name = scopeValue(registration.namespace_name, "public");
            impl_->registration_.group_name     = scopeValue(registration.group_name, "DEFAULT_GROUP");
            impl_->registration_requested_.store(true, ::std::memory_order_release);
            impl_->instance_id_.clear();
        }
        impl_->retry_delay_.store(::std::chrono::milliseconds { 1000 }, ::std::memory_order_release);
        impl_->next_registration_.store(Clock::time_point::min(), ::std::memory_order_release);
        if (impl_->state_machine_.state() == CatalogState::READY)
        {
            impl_->transition(CatalogState::REGISTERING, CatalogEventType::STATE_CHANGED, "registration updated");
        }
        impl_->kick();
        return {};
    }
} // namespace plane::catalog

// cy_psdk/manager/catalog/client/CatalogRuntime.h
//
// 自研 SwarmCatalog 客户端运行时公开入口 (由 swarm-catalog-client-java 的
// CatalogRuntime 转译)。业务用法:
//
//   CatalogRuntimeOptions options;
//   options.registration = ...;
//   options.event_callback = ...;
//   CatalogRuntime runtime(options, discovery_config);
//   runtime.start();                 // 同步发现, 成功时状态 DISCOVERED
//   runtime.registerServiceInstance(); // 注册当前实例 (内部重试/心跳)
//   ...
//   runtime.stop();
//
// 发现配置 (nodeId/port/targets) 由业务构造 DiscoveryConfig 注入, 不读取任何系统 yml。

#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "define.h"
#include "manager/catalog/client/CatalogModels.h"
#include "manager/catalog/client/CatalogRuntimeOptions.h"
#include "manager/catalog/client/CatalogTypes.h"
#include "manager/catalog/client/ConfigSubscription.h"
#include "manager/catalog/client/DiscoveryConfig.h"
#include "manager/catalog/client/Result.h"

namespace plane::catalog
{
    namespace internal
    {
        class DiscoveryClient;
        class HttpTransport;
    } // namespace internal

    // 业务进程内的服务目录客户端唯一公开入口。
    // 线程模型: start() 后存在 1 个控制线程 (发现/注册/心跳/状态/配置调度)
    // 与 1 个回调线程。业务线程可并发调用查询/配置方法 (各自独立发 HTTP)。
    // 回调在回调线程串行执行, 禁止在回调内同步等待 stop() 或做阻塞 IO。
    class CatalogRuntime
    {
    public:
        // 使用默认实现 (cpp-httplib HTTP + UDP 探测)
        CatalogRuntime(CatalogRuntimeOptions options, DiscoveryConfig discovery_config);

        // 注入自定义传输/发现实现 (单元测试用)。HttpTransport/DiscoveryClient 在此仅前向声明,
        // 按值传递 unique_ptr 时调用方需含其完整定义才能析构形参, 因此刻意不提供默认实参 ——
        // 在不完整类型上生成 unique_ptr 默认实参是 GCC 容忍、Clang 报错的可移植性陷阱。
        CatalogRuntime(
            CatalogRuntimeOptions                        options,
            DiscoveryConfig                              discovery_config,
            ::std::unique_ptr<internal::HttpTransport>   http_transport,
            ::std::unique_ptr<internal::DiscoveryClient> discovery_client
        );

        // 析构 best-effort 停止 (等价 stop(2s))
        ~CatalogRuntime(void);

        CatalogRuntime(const CatalogRuntime&)            = delete;
        CatalogRuntime& operator=(const CatalogRuntime&) = delete;

        // 同步发现唯一 Catalog 并启动后台维护。成功返回时状态为 Discovered。
        // 重复调用返回 AlreadyStarted。
        [[nodiscard]] Result<void> start(void);

        // 请求注册当前服务实例。注册与重试由控制线程执行。
        [[nodiscard]] Result<void> registerServiceInstance(void);

        // 停止后台线程并 best-effort 注销。timeout 限制注销等待。
        [[nodiscard]] Result<void>                     stop(::std::chrono::milliseconds timeout = ::std::chrono::seconds(5));

        [[nodiscard]] CatalogState                     state(void) const;
        [[nodiscard]] ::std::optional<CatalogEndpoint> catalogEndpoint(void) const;
        [[nodiscard]] ::std::string                    instanceId(void) const;

        // 上报最新健康状态 (快照式)。由调用方周期调用保持最新。
        [[nodiscard]] Result<void>            updateStatus(const ServiceStatus& status);

        [[nodiscard]] Result<ResolvedService> resolveService(const ServiceQuery& query);
        [[nodiscard]] Result<ServicePage>
            listServices(const ::std::string& namespace_name, const ::std::string& service_name, int page, int page_size);
        [[nodiscard]] Result<ServiceStatus> getInstanceStatus(
            const ::std::string& namespace_name,
            const ::std::string& group_name,
            const ::std::string& service_id,
            const ::std::string& instance_id
        );

        [[nodiscard]] Result<::std::string>                 getLocalIp(void);
        [[nodiscard]] Result<CatalogServerInfo>             getCatalogServerInfo(void);

        [[nodiscard]] Result<ConfigDocument>                putConfig(const ConfigUploadRequest& request);
        [[nodiscard]] Result<::std::vector<ConfigDocument>> getConfig(const ConfigQuery& query);
        [[nodiscard]] Result<ConfigSubscription> watchConfig(const ConfigKey& key, ::std::function<void(const ConfigChangeEvent&)> callback);

        [[nodiscard]] Result<void>               updateLogPaths(const ::std::vector<LogPath>& paths);
        [[nodiscard]] Result<void>               updateRegistration(const ServiceRegistration& registration);

    private:
        struct Impl;
        ::std::unique_ptr<Impl> impl_ {};
    };
} // namespace plane::catalog

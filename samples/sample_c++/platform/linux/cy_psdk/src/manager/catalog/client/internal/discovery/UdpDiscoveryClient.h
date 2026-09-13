// cy_psdk/manager/catalog/client/internal/discovery/UdpDiscoveryClient.h
//
// UDP 目录发现客户端 (对齐 java UdpDiscoveryClient, 传输层用 asio)。
// 同步 discover: 向 targets 广播探测请求, 在 response_window 内收集响应并去重。

#pragma once

#include <atomic>
#include <memory>
#include <optional>
#include <string>

#include "define.h"
#include "manager/catalog/client/CatalogTypes.h"
#include "manager/catalog/client/DiscoveryConfig.h"
#include "manager/catalog/client/internal/discovery/CatalogIpCache.h"
#include "manager/catalog/client/internal/discovery/DiscoveryClient.h"
#include "manager/catalog/client/internal/discovery/DiscoveryReport.h"

namespace plane::catalog::internal
{
    class UdpDiscoveryClient final: public DiscoveryClient
    {
    public:
        // cache_file 为空则禁用 IP 持久化缓存; bind_address 为空则通配绑定
        explicit UdpDiscoveryClient(::std::optional<::std::string> cache_file = ::std::nullopt, ::std::string bind_address = "");
        ~UdpDiscoveryClient(void)                                = default;

        UdpDiscoveryClient(const UdpDiscoveryClient&)            = delete;
        UdpDiscoveryClient& operator=(const UdpDiscoveryClient&) = delete;

        // 同步探测; cancelled 置位时尽快返回 (NOT_FOUND)
        [[nodiscard]] DiscoveryReport discover(const DiscoveryConfig& config, const ::std::atomic<bool>& cancelled) override;

        // 注册成功后记录最近可用目录 IP (供下次探测优先)
        void saveSuccessfulIp(const ::std::string& ip) override;

    private:
        [[nodiscard]] static DiscoveryReport invalid(const ::std::string& message);
        [[nodiscard]] static DiscoveryReport notFound(const ::std::string& message);
        [[nodiscard]] static DiscoveryReport socketError(const ::std::string& message);

        ::std::unique_ptr<CatalogIpCache>    cache_ {};
        ::std::string                        bind_address_ {};
    };
} // namespace plane::catalog::internal

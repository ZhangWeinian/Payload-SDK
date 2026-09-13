// cy_psdk/manager/catalog/client/CatalogTypes.h
//
// 生命周期状态、事件类型/事件与目录端点等核心类型 (对齐 java model 包)。

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "define.h"

namespace plane::catalog
{
    // 运行时生命周期状态 (对齐 java CatalogState)
    enum class CatalogState
    {
        STOPPED = 0,
        DISCOVERING,
        DISCOVERED,
        UNAVAILABLE,
        CONFLICT,
        REGISTERING,
        READY,
        STOPPING
    };

    // 运行时事件类型 (对齐 java CatalogEventType)
    enum class CatalogEventType
    {
        DISCOVERY_SUCCEEDED = 0,
        DISCOVERY_FAILED,
        DISCOVERY_CONFLICT,
        CATALOG_LOST,
        REGISTRATION_SUCCEEDED,
        REGISTRATION_FAILED,
        REGISTRATION_RESTORED,
        STATUS_REPORT_FAILED,
        CONFIG_FETCH_FAILED,
        STATE_CHANGED
    };

    // 目录端点。node_name 来自 UDP 响应 (旧服务端可为空);
    // multicast 字段为组播配置占位 (UDP 探测暂不解析, 由 getCatalogServerInfo 提供权威值)。
    struct CatalogEndpoint
    {
        ::std::string instance_id {};
        ::std::string ip {};
        int           http_port { 0 };
        ::std::string node_name {};
        ::std::string multicast_address {};
        int           multicast_port { 0 };
    };

    // 运行时状态事件 (对齐 java CatalogEvent)
    struct CatalogEvent
    {
        CatalogEventType                 type { CatalogEventType::STATE_CHANGED };
        CatalogState                     previous_state { CatalogState::STOPPED };
        CatalogState                     current_state { CatalogState::STOPPED };
        ::std::string                    message {};
        ::std::optional<CatalogEndpoint> endpoint {};
        ::std::vector<CatalogEndpoint>   conflict_endpoints {};
    };

    // 当前已连接 Catalog 服务端通过 GET /api/udp-config 报告的完整身份与组播配置
    struct CatalogServerInfo
    {
        ::std::string ip {};                // Catalog 服务端自身 IP
        ::std::string node_id {};           // Catalog 节点 ID
        ::std::string node_name {};         // Catalog 节点显示名称
        ::std::string multicast_ip {};      // 服务端 multicastIp 原始值 (数值响应也归一化为字符串)
        ::std::string multicast_address {}; // 服务端组播地址
    };
} // namespace plane::catalog

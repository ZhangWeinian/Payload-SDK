// cy_psdk/manager/catalog/client/CatalogModels.h
//
// 业务模型值类型 (由 swarm-catalog-client-java model 包转译, 字段语义一一对应)。

#pragma once

#include <chrono>
#include <map>
#include <string>
#include <vector>

#include "define.h"
#include "manager/catalog/client/CatalogTypes.h"

namespace plane::catalog
{
    // 暴露端口声明。url 为可选完整访问地址 (如 RTSP 推流 rtsp://user:pass@ip:port/path,
    // 含凭证与路径); 为空时服务端按 protocol/port 自行拼接。
    // ip/path 为端点业务访问地址与路径; ip 为空时服务端用实例来源 IP。
    struct ExposedPort
    {
        ::std::string name {};
        ::std::string protocol {};
        int           port { 0 };
        ::std::string url {}; // 可选完整访问地址
        ::std::string ip {};  // 端点业务访问地址 (为空时服务端用实例来源 IP)
        ::std::string path {};
    };

    // 允许服务目录查询的日志路径。path 必须为绝对路径。
    struct LogPath
    {
        ::std::string name {};
        ::std::string path {};
        bool          recursive { true };
        ::std::string file_pattern {};
    };

    // 当前业务服务的期望注册信息
    struct ServiceRegistration
    {
        ::std::string              namespace_name {}; // 为空时归一化为 public
        ::std::string              group_name {};     // 为空时归一化为 DEFAULT_GROUP
        ::std::string              service_id {};     // 稳定且唯一的技术标识
        ::std::string              service_name {};   // 供页面和接口展示的服务名称
        ::std::string              version {};        // 必填, 去除首尾空白后最长 128 字符
        ::std::vector<ExposedPort> exposed_ports {};
        ::std::vector<LogPath>     log_paths {};
        ::std::string              metadata_json {}; // 对象 JSON 或空
    };

    // 单个业务组件健康状态
    struct ServiceComponentStatus
    {
        ::std::string                            name {};
        ::std::string                            status {}; // UP / DEGRADED / DOWN / UNKNOWN
        ::std::string                            code {};
        ::std::string                            message {};
        ::std::map<::std::string, ::std::string> details {};
    };

    // 业务健康状态。运行时只保留最新一份。
    struct ServiceStatus
    {
        bool                                     healthy { true };
        ::std::string                            overall_status {}; // 为空时根据 healthy 生成 UP/DOWN
        ::std::string                            code {};
        ::std::string                            message {};
        ::std::map<::std::string, ::std::string> details {};
        ::std::vector<ServiceComponentStatus>    components {};
        ::std::chrono::system_clock::time_point  occurred_at {};
    };

    // 服务查询条件。为空字段继承注册作用域。
    struct ServiceQuery
    {
        ::std::string namespace_name {};
        ::std::string group_name {};
        ::std::string service_id {};
        ::std::string service_name {}; // service_id 为空时作为技术标识
    };

    // 服务三元组标识 (namespace / group / serviceId)
    struct ServiceKey
    {
        ::std::string namespace_name {};
        ::std::string group_name {};
        ::std::string service_id {};
    };

    // 已注册服务 (不含实例)
    struct RegisteredService
    {
        ServiceKey    key {};
        ::std::string service_name {};
        ::std::string source {};
    };

    // 服务摘要: 服务信息 + 实例统计 + 运行状态
    struct ServiceSummary
    {
        RegisteredService service {};
        long long         total_instances { 0 };
        long long         healthy_instances { 0 };
        ::std::string     runtime_status {};
    };

    // 服务分页查询结果 (GET /api/registry/services)
    struct ServicePage
    {
        ::std::vector<ServiceSummary> items {};
        long long                     total_elements { 0 };
        int                           page { 1 };
        int                           page_size { 0 };
    };

    // 服务实例端点。primary=服务端选定的主实例 (旧服务端缺省 false);
    // enabled=是否启用 (缺省 true)。消费方应优先选择 primary=true 的实例。
    struct ServiceEndpoint
    {
        ::std::string              instance_id {};
        ::std::string              address {}; // 实例身份 IP (业务访问地址应从 exposed_ports 中选择)
        ::std::string              version {};
        ::std::vector<ExposedPort> exposed_ports {};
        ::std::string              metadata_json {};
        bool                       primary { false };
        bool                       enabled { true };
    };

    // 服务解析结果: 全部健康且启用的实例
    struct ResolvedService
    {
        ::std::vector<ServiceEndpoint> endpoints {};
    };

    // 配置三元组标识 (namespace / group / dataId)
    struct ConfigKey
    {
        ::std::string namespace_name {};
        ::std::string group_name {};
        ::std::string data_id {};

        bool          operator<(const ConfigKey& other) const noexcept
        {
            if (namespace_name != other.namespace_name)
            {
                return namespace_name < other.namespace_name;
            }
            if (group_name != other.group_name)
            {
                return group_name < other.group_name;
            }
            return data_id < other.data_id;
        }
    };

    // 批量配置查询。一次请求中的配置必须属于同一个 namespace/group。
    struct ConfigQuery
    {
        ::std::string                namespace_name {};
        ::std::string                group_name {};
        ::std::vector<::std::string> data_ids {};
    };

    // 通用配置上传请求。SDK 按 key 原样保存完整 content, 不解析业务配置内容。
    struct ConfigUploadRequest
    {
        ConfigKey     key {};
        ::std::string content {};
        ::std::string format {};
    };

    // 配置文档。from_cache=true 表示最后一次有效缓存, 不能当成目录最新数据。
    struct ConfigDocument
    {
        ConfigKey     key {};
        ::std::string content {};
        ::std::string format {};
        ::std::string version {};
        ::std::string updated_at {};
        bool          from_cache { false };
    };

    // 配置变更事件。只在首次获取或内容真实变化时投递。
    struct ConfigChangeEvent
    {
        ConfigDocument previous {};
        ConfigDocument current {};
        bool           initial_load { false };
    };

    // Catalog 服务端主动广播的公告信息 (SWMP command=0x82)
    struct CatalogAnnouncement
    {
        ::std::string node_id {};
        ::std::string ip {}; // 为空时由接收方回退为 UDP 源地址
        ::std::string node_name {};
        ::std::string instance_id {};
        int           http_port { 0 };
    };
} // namespace plane::catalog

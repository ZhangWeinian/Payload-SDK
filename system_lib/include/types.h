#pragma once

#include "error.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace swarm {
namespace catalog {

/// 运行时生命周期。发现成功后即可查询；实例注册成功后才允许状态上报。
enum class CatalogState {
    Stopped,     ///< 未启动或已停完。
    Discovering, ///< 正在探测。禁止注册/上报；查询返回不可用。
    Discovered,  ///< 已发现唯一目录，可查询配置和服务，尚未注册实例。
    Unavailable, ///< 目录不可达。查询返回不可用；配置可返回缓存。
    Conflict,    ///< 多个目录。禁止远程写；查询返回 CatalogConflict；配置不刷新。
    Registering, ///< 内部注册中。允许查询服务与配置。
    Ready,       ///< 允许上报、查询和配置刷新。
    Stopping     ///< 正在停止。
};

struct ExposedPort {
    std::string name;
    std::string protocol;
    std::uint16_t port = 0;
    /// 端点业务访问地址；注册时为空则由服务端使用 HTTP 来源 IP 回填。
    /// 可填写组播、串口、设备标识或其他非单播业务地址，不要求与实例身份 IP 相同。
    std::string ip;
    /// 端点路径，例如 /api、/live/stream；可为空。
    std::string path;
};

/// 允许服务目录查询的日志路径。只含路径信息，禁止填写 SSH 用户名、密码、私钥或 SSH 端口。
struct LogPath {
    std::string name;
    std::string path; ///< 必须是绝对路径。
    bool recursive = true;
    std::string file_pattern;
};

/// 当前业务服务的期望注册信息。变化后由运行时重新提交，不必业务自己发 HTTP。
struct ServiceRegistration {
    /// 运行时默认命名空间；为空时归一化为 public。
    std::string namespace_name;
    /// 运行时默认分组；为空时归一化为 DEFAULT_GROUP。
    std::string group_name;
    std::string service_id;   ///< 稳定且唯一的技术标识，用于所有服务接口的 URL。
    std::string service_name; ///< 供页面和接口展示的服务名称。

    /// 当前应用或服务进程版本。注册时必填，去除首尾空白后最长 128 个字符。
    std::string version;

    /// 业务端点列表；注册时可为空。
    std::vector<ExposedPort> exposed_ports;
    std::vector<LogPath> log_paths;

    std::string metadata_json; ///< 必须是对象 JSON 或空；非法 JSON 导致注册失败。
};

/// 业务健康状态。正常与异常共用此模型。运行时只保留最新一份，不排队历史。
struct ServiceComponentStatus {
    std::string name;
    std::string status; ///< UP、DEGRADED、DOWN 或 UNKNOWN。
    std::string code;
    std::string message;
    std::string details_json;
};

struct ServiceStatus {
    bool healthy = true;
    std::string overall_status; ///< 为空时根据 healthy 生成 UP 或 DOWN。
    std::string code;
    std::string message;
    std::string details_json;
    std::vector<ServiceComponentStatus> components;
    /// 预留字段；2.0.0 暂未写入状态上报正文。
    std::chrono::system_clock::time_point occurred_at{};
};

struct ServiceQuery {
    /// 为空时继承 CatalogRuntime 初始化时的注册命名空间和分组。
    std::string namespace_name;
    std::string group_name;
    std::string service_id;
    std::string service_name; ///< 兼容旧调用；service_id 为空时作为技术标识使用。
};

struct ServiceEndpoint {
    std::string instance_id;
    std::string instance_ip; ///< 实例身份 IP；业务访问地址应从 exposed_ports 中选择。
    std::string version;     ///< 实例注册时上报的应用版本；服务端未返回时为空。
    std::vector<ExposedPort> exposed_ports;
    std::string metadata_json;
    bool primary = false; ///< 是否为服务端选定的主实例；旧服务端未返回时为 false。
    bool enabled = true;  ///< 是否启用；缺失时按 true 兼容旧服务端。
};

/// 查询结果包含全部已启用且健康的实例，不只返回第一个地址。
/// 消费方应优先选择 primary=true 的实例，并保留其他实例作为备用。
struct ResolvedService {
    std::vector<ServiceEndpoint> endpoints;
};

struct ConfigKey {
    /// 为空时继承 CatalogRuntime 初始化时的注册命名空间和分组。
    std::string namespace_name;
    std::string group_name;
    std::string data_id;
};

/// 批量配置查询。一次请求中的配置必须属于同一个 namespace/group。
struct ConfigQuery {
    /// 为空时继承 CatalogRuntime 初始化时的注册命名空间和分组。
    std::string namespace_name;
    std::string group_name;
    std::vector<std::string> data_ids;
};

/// 通用配置上传请求。SDK 按 key 原样保存完整 content，不解析或修改业务配置内容。
/// namespace_name/group_name 为空时继承 CatalogRuntime 初始化时的注册作用域。
struct ConfigUploadRequest {
    ConfigKey key;
    std::string content;
    std::string format;
};

/// 配置内容为字符串，公共 API 不暴露 JSON 类型。
/// from_cache=true 表示最后一次有效缓存，不能当成目录最新数据。
struct ConfigDocument {
    ConfigKey key;
    std::string content;
    std::string format;
    std::string version;
    std::string updated_at;
    bool from_cache = false;
};

struct CatalogEndpoint {
    std::string instance_id;
    std::string ip;
    std::uint16_t http_port = 0;
    std::string node_name; ///< 目录节点显示名，来自 UDP 响应；旧服务端可为空。
    std::string multicast_address;  ///< 组播地址，来自 UDP 响应；旧服务端可为空。
    std::uint16_t multicast_port = 0;  ///< 组播端口，旧服务端为 0。
};

/// 当前已连接 Catalog 服务端通过 /api/udp-config 报告的完整身份与组播配置。
struct CatalogServerInfo {
    std::string ip;                ///< Catalog 服务端自身 IP。
    std::string node_id;           ///< Catalog 节点 ID。
    std::string node_name;         ///< Catalog 节点显示名称。
    std::string multicast_ip;      ///< 服务端 multicastIp 原始值，数值响应也归一化为字符串。
    std::string multicast_address; ///< 服务端组播地址。
};

/// 节点清单中一条关系边：当前节点看对方为 view，对方看当前节点为 other_view。
struct NodeListRelationPair {
    std::string other_id;
    std::string other_name;
    std::string view;       ///< SUPERIOR / SUBORDINATE / NEIGHBOR
    std::string other_view;
};

/// 当前节点与某一对端的授权：outbound 是本节点开放给对方，inbound 是对方开放给本节点。
struct NodeListPeerGrant {
    std::string peer_node_id;
    std::string peer_node_name;
    std::string view;
    std::string other_view;
    std::vector<std::string> outbound_operations;
    std::vector<std::string> inbound_operations;
};

struct NodeListAuthorization {
    std::vector<NodeListRelationPair> pairs;
    std::vector<NodeListPeerGrant> grants;
    std::int64_t network_revision = 0;
    std::string owner_node_id;
};

struct NodeListStatus {
    bool online = false;
    std::string label;
    std::int64_t response_millis = 0;
    int missed_scans = 0;
};

struct NodeListConfigVersion {
    std::int64_t classification_revision = 0;
    std::int64_t authorization_revision = 0;
};

/// 节点清单一行：清单列相对本机，authorization 是该节点对全网。
struct NodeListEntry {
    std::string node_id;
    std::string node_name;
    std::string address;
    std::string relation; ///< LOCAL / SUPERIOR / SUBORDINATE / NEIGHBOR / OTHER
    NodeListConfigVersion config_version;
    std::vector<std::string> effective_permissions;
    std::string mqtt;
    NodeListStatus status;
    NodeListAuthorization authorization;
};

/// GET /api/datapool/v1/discovery/node-list 解析后的节点清单。
struct NodeList {
    std::string local_node_id;
    std::vector<NodeListEntry> nodes;
};

/// GET /api/datapool/v1/data?key=... 解析后的数据池条目（payload 已 Base64 解码）。
struct DataPoolValue {
    std::string key;
    std::string content_type;
    std::vector<std::uint8_t> payload;
    std::int64_t version = 0;
    std::string source_node_id;
    std::string updated_at;
};

enum class CatalogEventType {
    DiscoverySucceeded,    ///< 探测到唯一目录。
    DiscoveryFailed,       ///< 首次未发现目录。
    DiscoveryConflict,     ///< 多个目录；conflict_endpoints 含全部实例。
    CatalogLost,           ///< 目录离线或连续失败达阈值。
    RegistrationSucceeded, ///< 注册成功。
    RegistrationFailed,    ///< 单次注册失败。
    RegistrationRestored,  ///< 冲突/离线恢复后重新进入注册。
    StatusReportFailed,    ///< 心跳或状态上报失败。
    ConfigFetchFailed,     ///< 配置拉取失败，不走配置变更回调。
    StateChanged           ///< 其它状态迁移。
};

/// 运行时状态事件，描述目录与客户端运行状态，不携带配置正文。
struct CatalogEvent {
    CatalogEventType type = CatalogEventType::StateChanged;
    CatalogState previous_state = CatalogState::Stopped;
    CatalogState current_state = CatalogState::Stopped;
    std::string message;
    std::optional<CatalogEndpoint> endpoint;
    std::vector<CatalogEndpoint> conflict_endpoints;
};

/// 运行时事件回调。在回调线程执行。禁止同步等待 stop()，禁止阻塞 IO。抛出异常会被吞掉。
using RuntimeEventCallback = std::function<void(const CatalogEvent &)>;

/// 配置变更事件。只在首次获取或内容真实变化时投递。获取失败不会伪装成空配置变更。
struct ConfigChangeEvent {
    ConfigDocument previous;
    ConfigDocument current;
    bool initial_load = false;
};

/// 配置变更回调。与 RuntimeEventCallback 分开订阅。同一配置项回调保序。抛出异常会被吞掉。
using ConfigChangeCallback = std::function<void(const ConfigChangeEvent &)>;

/// 构造 CatalogRuntime 的业务参数。目录发现参数由客户端读取 /etc/catalog.yml。
struct CatalogRuntimeOptions {
    ServiceRegistration registration;
    std::chrono::milliseconds http_timeout{10000}; ///< 连接与总请求超时，非正值使用默认 10 秒。
    std::chrono::milliseconds heartbeat_interval{3000};
    std::chrono::milliseconds config_check_interval{5000};
    /// 连续可重试失败达到该次数后才进入 Unavailable。单次超时不直接判定目录离线。
    std::size_t unavailable_failure_threshold = 3;
    RuntimeEventCallback event_callback;
    /// 仅测试：覆盖 UDP 探测。空=未发现，1 个=成功，多个=冲突。业务代码不要设置。
    std::function<std::vector<CatalogEndpoint>()> test_discover;
};

} // namespace catalog
} // namespace swarm

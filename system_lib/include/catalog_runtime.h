#pragma once

#include "error.h"
#include "types.h"

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace swarm {
namespace catalog {

class HttpTransport;

/// 配置监听句柄。
///
/// 线程安全：cancel() 与析构可从业务线程调用。取消后对应配置项不再投递 ConfigChangeCallback。
/// 回调禁令：不要在配置回调或运行时事件回调里同步等待 stop() 完成，也不要在回调里再 cancel 同一订阅造成重入死锁风险。
class ConfigSubscription {
public:
    ConfigSubscription() = default;
    ~ConfigSubscription();
    ConfigSubscription(ConfigSubscription&& other) noexcept;
    ConfigSubscription& operator=(ConfigSubscription&& other) noexcept;
    ConfigSubscription(const ConfigSubscription&) = delete;
    ConfigSubscription& operator=(const ConfigSubscription&) = delete;

    /// 取消监听。重复调用是安全的。析构时也会取消。
    void cancel();
    bool valid() const { return static_cast<bool>(cancel_); }

private:
    friend class CatalogRuntime;
    std::function<void()> cancel_;
};

/// 业务进程内的服务目录客户端唯一公开入口。
///
/// 线程模型：start() 之后存在 1 个控制线程（发现/注册/心跳/状态/配置调度）和 1 个回调线程。
/// 业务线程可并发调用本类方法；查询、getConfig、putConfig、getLocalIp、
/// getCatalogServerInfo、getNodeList、getValue 与 getDataValue 在调用线程发 HTTP，
/// 但受 http_timeout 约束。
/// 状态、目录地址和 instance_id 受内部锁保护。
///
/// 回调禁令：
/// - 运行时事件与配置变更使用不同回调，不得混用；
/// - 回调在独立线程串行执行，不持有内部锁；
/// - 回调中抛出的异常会被吞掉，不终止运行时；
/// - 禁止在回调线程同步等待 stop()；
/// - 慢回调不能阻塞发现、注册和状态上报（因此不要在回调里做阻塞 IO）。
///
/// 错误行为：Discovered/Registering/Ready 均可查询，不阻塞等待目录。
/// Conflict 返回 CatalogConflict；Stopped 返回 Stopped；其余状态返回 CatalogUnavailable。
class CatalogRuntime {
public:
    /// 只保存业务参数。空的 service_id / service_name / version 或超过 128 字符的 version，
    /// 以及 /etc/catalog.yml 内部发现配置非法时，在 start() 时返回 InvalidArgument。
    /// 不启动线程、不执行网络请求。
    explicit CatalogRuntime(CatalogRuntimeOptions options);
    /// 仅测试：注入 HTTP 传输。业务代码不要使用。
    CatalogRuntime(CatalogRuntimeOptions options, std::unique_ptr<HttpTransport> transport);
    /// 析构会尽力 stop；注销网络请求有短超时，不会无限等待。
    ~CatalogRuntime();

    CatalogRuntime(const CatalogRuntime&) = delete;
    CatalogRuntime& operator=(const CatalogRuntime&) = delete;

    /// 从 /etc/catalog.yml 加载发现配置并同步探测唯一 Catalog，然后启动后台维护。
    /// 成功返回时状态为 Discovered，尚未注册当前服务实例。
    Result<void> start();
    /// 请求注册当前服务实例。注册与重试在控制线程执行；通过 Ready 状态或
    /// RegistrationSucceeded 事件确认注册完成。
    Result<void> registerServiceInstance();
    /// 停止后台线程并 best-effort 注销。timeout 限制注销 HTTP 等待时间。
    /// 未启动时返回 NotStarted。可从业务线程调用；不要从回调线程同步等待本函数。
    Result<void> stop(std::chrono::milliseconds timeout = std::chrono::seconds(5));

    /// 当前生命周期状态。可从任意线程读取。
    CatalogState state() const;
    /// 当前唯一有效目录地址。冲突或未发现时为空。
    std::optional<CatalogEndpoint> catalogEndpoint() const;
    /// 注册成功后的实例 ID。未注册时为空。
    std::string instanceId() const;

    /// 查询当前 HTTP 连接被 Catalog 识别到的客户端来源 IPv4，与服务注册无关。
    /// Discovered/Registering/Ready 状态均可调用；非法响应返回 ProtocolError；
    /// 目录不可用或超时返回对应的可重试错误。Ready 状态下遇到旧服务端 404 时会回退旧路径。
    Result<std::string> getLocalIp();

    /// 无查询条件获取当前已连接 Catalog 的服务端身份和组播配置。
    /// 调用 GET /api/udp-config，不接收 namespace、group、service_id 或实例 ID。
    /// Discovered/Registering/Ready 状态可调用；响应缺字段或类型错误返回 ProtocolError。
    Result<CatalogServerInfo> getCatalogServerInfo();

    /// 拉取当前 Catalog 发现的节点清单（含查看授权）。
    /// 调用 GET /api/datapool/v1/discovery/node-list，解析 key=nodeList 的 JSON value。
    /// Discovered/Registering/Ready 状态可调用；响应格式错误返回 ProtocolError。
    /// 业务若只需数据池快照原文，优先使用 getValue("nodeList")。
    Result<NodeList> getNodeList();

    /// 按 key 读取数据池条目的 UTF-8 正文（解码 payload）。
    /// 调用 GET /api/datapool/v1/data?key=<url-encoded>。空 key→InvalidArgument；
    /// 404→DataNotFound；非法 Base64/大小不一致或 payload 非合法 UTF-8→ProtocolError。
    /// Discovered/Registering/Ready 状态可调用。
    Result<std::string> getValue(const std::string& key);

    /// 按 key 读取完整数据池条目（含 contentType、version、原始字节 payload）。
    /// HTTP 与状态门禁同 getValue；payload 不做 UTF-8 校验。
    Result<DataPoolValue> getDataValue(const std::string& key);

    /// 更新本地最新状态快照。Ready 时由控制线程上报；离线期间不积压历史，只保留最后一次。
    /// 未启动返回 NotStarted。
    Result<void> updateStatus(ServiceStatus status);

    /// 按 namespace/group/service 查询全部已启用且健康的实例，并返回 primary 标记。
    /// Discovered/Registering/Ready 均可调用；其余状态快速失败。
    /// 服务不存在→ServiceNotFound；非法 JSON→ProtocolError。
    Result<ResolvedService> resolveService(const ServiceQuery& query);

    /// 在一次 HTTP 请求中拉取同一 namespace/group 下的多个配置，结果顺序与 data_ids 一致。
    /// data_ids 为空、含空值或重复值时返回 InvalidArgument。
    /// 拉取失败或目录不可用时，仅当全部配置都有最后一次有效缓存才返回 from_cache=true 的结果；
    /// 不返回部分结果。Stopped 返回 Stopped。任一配置不存在返回 ConfigNotFound。
    Result<std::vector<ConfigDocument>> getConfig(const ConfigQuery& query);

    /// 创建或更新任意配置。服务端不存在 key 时创建，已存在时以本次完整 content/format 更新；
    /// SDK 不会先查询配置，也不会解析或修改 YAML、JSON 等业务内容。
    ///
    /// key.data_id、content 或 format 为空，或者 format 含空白/控制字符时返回 InvalidArgument。
    /// namespace/group 为空时继承运行时注册作用域。Discovered、Registering、Ready 状态允许调用；
    /// 尚未 start 返回 NotStarted，停止后或 Stopping 返回 Stopped，Unavailable 返回
    /// CatalogUnavailable，Conflict 返回 CatalogConflict。
    ///
    /// 本方法为线程安全的同步接口，在调用线程中执行一次 HTTP PUT，并使用
    /// CatalogRuntimeOptions::http_timeout 限制等待时间。成功时返回服务端最终保存的文档，
    /// from_cache 恒为 false，同时更新 SDK 配置缓存和现有监听者所见版本。
    Result<ConfigDocument> putConfig(const ConfigUploadRequest& request);

    /// 登记监听。已有缓存时立即回调 initial_load=true；否则首次远程获取时回调。
    /// 之后仅内容或格式真实变化时回调；同内容的新版本会同步到缓存但不重复投递。
    /// 获取失败发 CatalogEventType::ConfigFetchFailed，不会向配置回调投递空文档。
    /// data_id 为空返回 InvalidArgument。
    Result<ConfigSubscription> watchConfig(const ConfigKey& key, ConfigChangeCallback callback);

    /// 更新日志路径快照并在 Ready 时触发重新注册；离线时保留到下一次注册。
    /// 路径必须为绝对路径（以 '/' 开头），否则 InvalidArgument。不含 SSH 凭据。
    /// 2.0.0 注册正文尚未携带 log_paths，服务端暂时收不到该快照。
    Result<void> updateLogPaths(std::vector<LogPath> log_paths);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace catalog
} // namespace swarm

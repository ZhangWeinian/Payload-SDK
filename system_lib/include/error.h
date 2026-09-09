#pragma once

#include <string>
#include <utility>

namespace swarm {
namespace catalog {

/// 本地错误分类。HTTP 状态码、服务端业务码和消息放在 Error 中，不塞进枚举。
enum class CatalogError {
    None,                  ///< 无错误。不要用默认 Error 表示一次成功的调用。
    InvalidArgument,       ///< 参数非法，不可重试。
    AlreadyStarted,        ///< start() 重复调用。
    NotStarted,            ///< 尚未 start 或已完全停止后的非法操作。
    CatalogUnavailable,    ///< 目录不可达或连续失败达阈值。可重试。
    CatalogConflict,       ///< 发现多个目录实例，禁止远程操作。
    DiscoveryTimeout,      ///< UDP 探测超时。可重试。
    RegistrationRejected,  ///< 注册数据被拒绝或真正的 409 冲突。不可重试。
    InstanceNotFound,      ///< 心跳/状态发现实例记录丢失，应重新注册。
    ServiceNotFound,       ///< 查询的服务不存在。
    ConfigNotFound,        ///< 查询的配置不存在。
    HttpError,             ///< 未单独归类的 HTTP 失败。默认可重试。
    ProtocolError,         ///< JSON 非法、缺字段或未知结构。不可重试。
    Timeout,               ///< 请求超时。可重试；单次超时不会单独把运行时打成 Unavailable。
    Stopped,               ///< 运行时正在停止或已停止。
    DataNotFound           ///< 数据池 key 不存在。追加在末尾，避免重排既有枚举值。
};

/// 一次失败的完整描述。
/// 必须保留：本地分类、HTTP 状态码、服务端业务码、服务端消息、是否允许重试。
/// 默认构造 code==None，只表示“空错误对象”，不表示业务调用成功。
struct Error {
    CatalogError code = CatalogError::None;
    int http_status = 0;
    std::string server_code;
    std::string message;
    bool retryable = false;
};

/// CatalogUnavailable / DiscoveryTimeout / Timeout / HttpError 为可重试。
inline bool isRetryable(CatalogError code) {
    switch (code) {
        case CatalogError::CatalogUnavailable:
        case CatalogError::DiscoveryTimeout:
        case CatalogError::Timeout:
        case CatalogError::HttpError:
            return true;
        default:
            return false;
    }
}

inline const char* errorMessage(CatalogError code) {
    switch (code) {
        case CatalogError::None:
            return "成功";
        case CatalogError::InvalidArgument:
            return "参数非法";
        case CatalogError::AlreadyStarted:
            return "运行时已启动";
        case CatalogError::NotStarted:
            return "运行时未启动";
        case CatalogError::CatalogUnavailable:
            return "服务目录不可用";
        case CatalogError::CatalogConflict:
            return "发现多个服务目录实例";
        case CatalogError::DiscoveryTimeout:
            return "目录探测超时";
        case CatalogError::RegistrationRejected:
            return "注册被拒绝";
        case CatalogError::InstanceNotFound:
            return "实例不存在";
        case CatalogError::ServiceNotFound:
            return "服务不存在";
        case CatalogError::ConfigNotFound:
            return "配置不存在";
        case CatalogError::HttpError:
            return "HTTP 请求失败";
        case CatalogError::ProtocolError:
            return "协议或 JSON 非法";
        case CatalogError::Timeout:
            return "请求超时";
        case CatalogError::Stopped:
            return "运行时已停止";
        case CatalogError::DataNotFound:
            return "数据池条目不存在";
        default:
            return "未知错误";
    }
}

inline Error makeError(CatalogError code, std::string message = {}, int http_status = 0,
                       std::string server_code = {}) {
    Error e;
    e.code = code;
    e.http_status = http_status;
    e.server_code = std::move(server_code);
    e.message = message.empty() ? errorMessage(code) : std::move(message);
    e.retryable = isRetryable(code);
    return e;
}

/// 必须通过 success() / failure() 显式构造。默认构造为失败，不会被当成成功。
/// ok() 为 false 时不要读取 value()。
template <typename T>
class Result {
public:
    static Result success(T value) {
        Result r;
        r.ok_ = true;
        r.value_ = std::move(value);
        return r;
    }

    static Result failure(Error err) {
        Result r;
        r.ok_ = false;
        r.error_ = std::move(err);
        return r;
    }

    bool ok() const { return ok_; }
    explicit operator bool() const { return ok_; }
    const T& value() const { return value_; }
    T& value() { return value_; }
    const Error& error() const { return error_; }

private:
    Result() = default;
    bool ok_ = false;
    T value_{};
    Error error_{};
};

template <>
class Result<void> {
public:
    static Result success() {
        Result r;
        r.ok_ = true;
        return r;
    }

    static Result failure(Error err) {
        Result r;
        r.ok_ = false;
        r.error_ = std::move(err);
        return r;
    }

    bool ok() const { return ok_; }
    explicit operator bool() const { return ok_; }
    const Error& error() const { return error_; }

private:
    Result() = default;
    bool ok_ = false;
    Error error_{};
};

}  // namespace catalog
}  // namespace swarm

// cy_psdk/manager/catalog/client/internal/service/ServiceGateway.cpp

#include "manager/catalog/client/internal/service/ServiceGateway.h"

#include <arpa/inet.h>
#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <string_view>
#include <set>

#include "manager/catalog/client/CatalogError.h"
#include "manager/catalog/client/CatalogFailure.h"
#include "manager/catalog/client/internal/codec/JsonCodec.h"

#include "define.h"

namespace plane::catalog::internal
{
    namespace
    {
        constexpr const char*               kRegistry     = "/api/registry/services";
        constexpr const char*               kConfigs      = "/api/configs";
        constexpr const char*               kConfigsBatch = "/api/configs/batch";
        constexpr const char*               kLocalIp      = "/api/registry/services/local-ip";
        constexpr const char*               kUdpConfig    = "/api/udp-config";

        [[nodiscard]] Result<::std::string> invalidString(const ::std::string& message)
        {
            return ::std::unexpected(makeFailure(CatalogError::INVALID_ARGUMENT, message));
        }

        [[nodiscard]] bool isValidIpv4(const ::std::string& value)
        {
            ::in_addr address {};
            return ::inet_pton(AF_INET, value.c_str(), &address) == 1;
        }

        // 大小写不敏感相等 (期望 upper 为大写形态; ASCII 协议字段)
        [[nodiscard]] bool asciiUpperEquals(::std::string_view value, ::std::string_view upper) noexcept
        {
            if (value.size() != upper.size())
            {
                return false;
            }
            for (::std::size_t index { 0 }; index < value.size(); ++index)
            {
                char ch { value[index] };
                if (ch >= 'a' && ch <= 'z')
                {
                    ch = static_cast<char>(ch - ('a' - 'A'));
                }
                if (ch != upper[index])
                {
                    return false;
                }
            }
            return true;
        }

        // 兼容旧服务端: 实例列表端点在 /api/registry/services 下统一
        [[nodiscard]] ::std::string encodeComponent(const ::std::string& value)
        {
            ::std::string result {};
            result.reserve(value.size());
            for (const unsigned char ch : value)
            {
                const bool safe { (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' ||
                                  ch == '.' || ch == '~' };
                if (safe)
                {
                    result.push_back(static_cast<char>(ch));
                }
                else
                {
                    constexpr static char kHex[] = "0123456789ABCDEF";
                    result.push_back('%');
                    result.push_back(kHex[(static_cast<unsigned>(ch) >> 4u) & 0X0fu]);
                    result.push_back(kHex[ch & 0X0fu]);
                }
            }
            return result;
        }
    } // namespace

    ServiceGateway::ServiceGateway(
        ::std::string                    catalog_url,
        ::std::unique_ptr<HttpTransport> http,
        ::std::chrono::milliseconds      timeout
    ): transport_(::std::move(catalog_url), ::std::move(http))
    {
        this->transport_.setTimeout(timeout);
    }

    [[nodiscard]] ::std::string ServiceGateway::scopeValue(const ::std::string& value, const ::std::string& fallback)
    {
        return value.empty() ? fallback : value;
    }

    [[nodiscard]] ::std::string
        ServiceGateway::instanceCollection(const ::std::string& namespace_name, const ::std::string& group_name, const ::std::string& service_id)
    {
        return ::std::string { kRegistry } + "/" + encodeComponent(scopeValue(namespace_name, "public")) + "/" +
               encodeComponent(scopeValue(group_name, "DEFAULT_GROUP")) + "/" + encodeComponent(service_id) + "/instances";
    }

    [[nodiscard]] Result<::std::string> ServiceGateway::registerInstance(const ServiceRegistration& registration)
    {
        Result<::nlohmann::json> body { JsonCodec::registrationToJson(registration, "") };
        if (!body.has_value())
        {
            return ::std::unexpected(body.error());
        }
        const ::std::string      path { instanceCollection(registration.namespace_name, registration.group_name, registration.service_id) };
        Result<::nlohmann::json> response { this->transport_.postJson(path, body.value().dump(), true) };
        if (!response.has_value())
        {
            return ::std::unexpected(remapNotFound(response.error(), CatalogError::INSTANCE_NOT_FOUND));
        }
        const ::nlohmann::json& json { response.value() };
        if (!json.contains("id") || !json["id"].is_string())
        {
            return ::std::unexpected(makeFailure(CatalogError::PROTOCOL_ERROR, "missing or invalid field: id"));
        }
        return json["id"].get<::std::string>();
    }

    [[nodiscard]] Result<void> ServiceGateway::heartbeat(const ServiceRegistration& registration, const ::std::string& instance_id)
    {
        const ::std::string      path { instanceCollection(registration.namespace_name, registration.group_name, registration.service_id) + "/" +
                                        encodeComponent(instance_id) + "/heartbeat" };
        Result<::nlohmann::json> response { this->transport_.postJson(path, "{}") };
        if (!response.has_value())
        {
            return ::std::unexpected(remapNotFound(response.error(), CatalogError::INSTANCE_NOT_FOUND));
        }
        const ::nlohmann::json& json { response.value() };
        if (!json.is_object())
        {
            return ::std::unexpected(makeFailure(CatalogError::PROTOCOL_ERROR, "heartbeat response must be an object"));
        }
        if (json.contains("id") && !json["id"].is_null() && !json["id"].is_string())
        {
            return ::std::unexpected(makeFailure(CatalogError::PROTOCOL_ERROR, "invalid type: id"));
        }
        return {};
    }

    [[nodiscard]] Result<void>
        ServiceGateway::reportStatus(const ServiceRegistration& registration, const ::std::string& instance_id, const ServiceStatus& status)
    {
        Result<::nlohmann::json> body { JsonCodec::statusToJson(status) };
        if (!body.has_value())
        {
            return ::std::unexpected(body.error());
        }
        const ::std::string path { instanceCollection(registration.namespace_name, registration.group_name, registration.service_id) + "/" +
                                   encodeComponent(instance_id) + "/status" };
        Result<void>        result { this->transport_.putVoid(path, body.value().dump()) };
        if (!result.has_value())
        {
            return ::std::unexpected(remapNotFound(result.error(), CatalogError::INSTANCE_NOT_FOUND));
        }
        return {};
    }

    [[nodiscard]] Result<void> ServiceGateway::deleteInstance(const ServiceRegistration& registration, const ::std::string& instance_id)
    {
        const ::std::string path { instanceCollection(registration.namespace_name, registration.group_name, registration.service_id) + "/" +
                                   encodeComponent(instance_id) };
        return this->transport_.del(path);
    }

    [[nodiscard]] Result<ResolvedService> ServiceGateway::resolve(const ServiceQuery& query)
    {
        const ::std::string service_id { query.service_id.empty() ? query.service_name : query.service_id };
        if (service_id.empty())
        {
            return ::std::unexpected(makeFailure(CatalogError::INVALID_ARGUMENT, "service_id is empty"));
        }
        Result<::nlohmann::json> response {
            this->transport_.getJson(instanceCollection(query.namespace_name, query.group_name, service_id) + "?healthyOnly=true")
        };
        if (!response.has_value())
        {
            return ::std::unexpected(remapNotFound(response.error(), CatalogError::SERVICE_NOT_FOUND));
        }
        const ::nlohmann::json& json { response.value() };
        if (!json.is_array())
        {
            return ::std::unexpected(makeFailure(CatalogError::PROTOCOL_ERROR, "instances must be an array"));
        }
        ResolvedService resolved {};
        for (const auto& item : json)
        {
            bool                    healthy { false };
            Result<ServiceEndpoint> parsed { parseEndpoint(item, healthy) };
            if (!parsed.has_value())
            {
                return ::std::unexpected(parsed.error());
            }
            if (healthy && parsed.value().enabled)
            {
                resolved.endpoints.push_back(::std::move(parsed.value()));
            }
        }
        return resolved;
    }

    [[nodiscard]] Result<ServicePage>
        ServiceGateway::listServices(const ::std::string& namespace_name, const ::std::string& service_name, int page, int page_size)
    {
        const int                safe_page { page < 1 ? 1 : page };
        const int                safe_page_size { page_size < 1 ? 20 : page_size };
        const ::std::string      path { ::fmt::format(
            "{}?namespace={}&serviceName={}&page={}&pageSize={}",
            kRegistry,
            encodeComponent(scopeValue(namespace_name, "public")),
            encodeComponent(service_name),
            safe_page,
            safe_page_size
        ) };
        Result<::nlohmann::json> response { this->transport_.getJson(path) };
        if (!response.has_value())
        {
            return ::std::unexpected(response.error());
        }
        const ::nlohmann::json& json { response.value() };
        if (!json.is_object() || !json.contains("items") || !json["items"].is_array())
        {
            return ::std::unexpected(makeFailure(CatalogError::PROTOCOL_ERROR, "service page items must be an array"));
        }
        ServicePage result {};
        for (const auto& item : json["items"])
        {
            Result<ServiceSummary> parsed { parseServiceSummary(item) };
            if (!parsed.has_value())
            {
                return ::std::unexpected(parsed.error());
            }
            result.items.push_back(::std::move(parsed.value()));
        }
        result.total_elements = json.value("totalElements", static_cast<long long>(result.items.size()));
        result.page           = json.value("page", safe_page);
        result.page_size      = json.value("pageSize", safe_page_size);
        return result;
    }

    [[nodiscard]] Result<ServiceStatus> ServiceGateway::getInstanceStatus(
        const ::std::string& namespace_name,
        const ::std::string& group_name,
        const ::std::string& service_id,
        const ::std::string& instance_id
    )
    {
        if (instance_id.empty())
        {
            return ::std::unexpected(makeFailure(CatalogError::INVALID_ARGUMENT, "instance_id is empty"));
        }
        const ::std::string path { instanceCollection(namespace_name, group_name, service_id) + "/" + encodeComponent(instance_id) + "/status" };
        Result<::nlohmann::json> response { this->transport_.getJson(path) };
        if (!response.has_value())
        {
            return ::std::unexpected(remapNotFound(response.error(), CatalogError::INSTANCE_NOT_FOUND));
        }
        return parseServiceStatus(response.value());
    }

    [[nodiscard]] Result<::std::string> ServiceGateway::getLocalIp(
        const ::std::string& namespace_name,
        const ::std::string& group_name,
        const ::std::string& service_id,
        bool                 allow_legacy_fallback
    )
    {
        Result<::nlohmann::json> response { this->transport_.getJson(kLocalIp) };
        if (!response.has_value() && response.error().http_status == 404 && allow_legacy_fallback)
        {
            const ::std::string legacy_path { ::std::string { kRegistry } + "/" + encodeComponent(scopeValue(namespace_name, "public")) + "/" +
                                              encodeComponent(scopeValue(group_name, "DEFAULT_GROUP")) + "/" + encodeComponent(service_id) +
                                              "/local-ip" };
            response = this->transport_.getJson(legacy_path);
            if (!response.has_value())
            {
                return ::std::unexpected(remapNotFound(response.error(), CatalogError::INSTANCE_NOT_FOUND));
            }
        }
        if (!response.has_value())
        {
            return ::std::unexpected(response.error());
        }
        const ::nlohmann::json& json { response.value() };
        if (!json.is_object())
        {
            return ::std::unexpected(makeFailure(CatalogError::PROTOCOL_ERROR, "local ip response must be an object"));
        }
        Result<::std::string> ip { requiredString(json, "ip") };
        if (!ip.has_value())
        {
            return ip;
        }
        if (!isValidIpv4(ip.value()))
        {
            return invalidString("invalid IPv4 address: " + ip.value());
        }
        return ip;
    }

    [[nodiscard]] Result<CatalogServerInfo> ServiceGateway::getCatalogServerInfo(void)
    {
        Result<::nlohmann::json> response { this->transport_.getJson(kUdpConfig) };
        if (!response.has_value())
        {
            return ::std::unexpected(response.error());
        }
        return parseCatalogServerInfo(response.value());
    }

    [[nodiscard]] Result<::std::vector<ConfigDocument>> ServiceGateway::getConfigs(const ConfigQuery& query)
    {
        Result<void> validation { validateDataIds(query.data_ids) };
        if (!validation.has_value())
        {
            return ::std::unexpected(validation.error());
        }
        ::nlohmann::json body;
        body["namespace"] = query.namespace_name;
        body["group"]     = query.group_name;
        body["dataIds"]   = ::nlohmann::json::array();
        for (const auto& id : query.data_ids)
        {
            body["dataIds"].push_back(id);
        }
        Result<::nlohmann::json> response { this->transport_.postJson(kConfigsBatch, body.dump()) };
        if (!response.has_value())
        {
            CatalogFailure failure { remapNotFound(response.error(), CatalogError::CONFIG_NOT_FOUND) };
            if (failure.http_status == 400)
            {
                failure = failureWithCode(failure, CatalogError::INVALID_ARGUMENT);
            }
            return ::std::unexpected(failure);
        }
        const ::nlohmann::json& json { response.value() };
        if (!json.is_array())
        {
            return ::std::unexpected(makeFailure(CatalogError::PROTOCOL_ERROR, "configs must be an array"));
        }

        ::std::set<::std::string>                 requested { query.data_ids.begin(), query.data_ids.end() };
        ::std::map<::std::string, ConfigDocument> by_id {};
        for (const auto& item : json)
        {
            Result<ConfigDocument> parsed { parseConfig(item) };
            if (!parsed.has_value())
            {
                return ::std::unexpected(parsed.error());
            }
            const ConfigDocument& document { parsed.value() };
            if (document.key.namespace_name != query.namespace_name || document.key.group_name != query.group_name)
            {
                return ::std::
                    unexpected(makeFailure(CatalogError::PROTOCOL_ERROR, "config scope does not match request: " + document.key.data_id));
            }
            if (requested.find(document.key.data_id) == requested.end())
            {
                return ::std::
                    unexpected(makeFailure(CatalogError::PROTOCOL_ERROR, "unexpected dataId in batch response: " + document.key.data_id));
            }
            if (by_id.find(document.key.data_id) != by_id.end())
            {
                return ::std::
                    unexpected(makeFailure(CatalogError::PROTOCOL_ERROR, "duplicate dataId in batch response: " + document.key.data_id));
            }
            by_id[document.key.data_id] = document;
        }
        ::std::vector<ConfigDocument> ordered {};
        for (const auto& id : query.data_ids)
        {
            const auto it { by_id.find(id) };
            if (it == by_id.end())
            {
                return ::std::unexpected(makeFailure(CatalogError::PROTOCOL_ERROR, "missing dataId in batch response: " + id));
            }
            ordered.push_back(it->second);
        }
        return ordered;
    }

    [[nodiscard]] Result<ConfigDocument> ServiceGateway::getConfig(const ConfigKey& key)
    {
        if (key.data_id.empty())
        {
            return ::std::unexpected(makeFailure(CatalogError::INVALID_ARGUMENT, "data_id is empty"));
        }
        const ::std::string      path { ::std::string { kConfigs } + "/" + encodeComponent(key.namespace_name) + "/" +
                                        encodeComponent(key.group_name) + "/" + encodeComponent(key.data_id) };
        Result<::nlohmann::json> response { this->transport_.getJson(path) };
        if (!response.has_value())
        {
            return ::std::unexpected(remapNotFound(response.error(), CatalogError::CONFIG_NOT_FOUND));
        }
        return parseConfig(response.value());
    }

    [[nodiscard]] Result<ConfigDocument> ServiceGateway::putConfig(const ConfigUploadRequest& request)
    {
        Result<void> validation { validateConfigUploadRequest(request) };
        if (!validation.has_value())
        {
            return ::std::unexpected(validation.error());
        }
        const ::std::string path { ::std::string { kConfigs } + "/" + encodeComponent(request.key.namespace_name) + "/" +
                                   encodeComponent(request.key.group_name) + "/" + encodeComponent(request.key.data_id) };
        ::nlohmann::json    body;
        body["content"] = request.content;
        body["format"]  = request.format;
        Result<::nlohmann::json> response { this->transport_.putJson(path, body.dump()) };
        if (!response.has_value())
        {
            CatalogFailure failure { response.error() };
            if (failure.http_status == 400)
            {
                failure = failureWithCode(failureWithRetryable(failure, false), CatalogError::INVALID_ARGUMENT);
            }
            else if (failure.http_status == 409)
            {
                failure = failureWithCode(failureWithRetryable(failure, false), CatalogError::CATALOG_CONFLICT);
            }
            return ::std::unexpected(failure);
        }
        Result<ConfigDocument> parsed { parseConfig(response.value()) };
        if (!parsed.has_value())
        {
            return parsed;
        }
        const ConfigDocument& saved { parsed.value() };
        if (saved.key.namespace_name != request.key.namespace_name || saved.key.group_name != request.key.group_name ||
            saved.key.data_id != request.key.data_id)
        {
            return ::std::unexpected(makeFailure(CatalogError::PROTOCOL_ERROR, "saved config key does not match upload request"));
        }
        if (saved.content != request.content || saved.format != request.format)
        {
            return ::std::unexpected(makeFailure(CatalogError::PROTOCOL_ERROR, "saved config content or format does not match upload request"));
        }
        if (saved.version.empty() || saved.updated_at.empty())
        {
            return ::std::unexpected(makeFailure(CatalogError::PROTOCOL_ERROR, "saved config is missing version or updatedAt"));
        }
        return parsed;
    }

    [[nodiscard]] Result<ServiceEndpoint> ServiceGateway::parseEndpoint(const ::nlohmann::json& json, bool& healthy)
    {
        healthy = false;
        if (!json.is_object())
        {
            return ::std::unexpected(makeFailure(CatalogError::PROTOCOL_ERROR, "instance must be an object"));
        }
        Result<::std::string> id { requiredString(json, "id") };
        if (!id.has_value())
        {
            return ::std::unexpected(id.error());
        }
        Result<::std::string> ip { requiredString(json, "ip") };
        if (!ip.has_value())
        {
            return ::std::unexpected(ip.error());
        }
        ::std::string version {};
        if (json.contains("version") && !json["version"].is_null())
        {
            if (!json["version"].is_string())
            {
                return ::std::unexpected(makeFailure(CatalogError::PROTOCOL_ERROR, "invalid type: version"));
            }
            version = json["version"].get<::std::string>();
        }
        healthy = !json.contains("healthy") || !json["healthy"].is_boolean() || json["healthy"].get<bool>();

        ServiceEndpoint endpoint {};
        endpoint.instance_id = id.value();
        endpoint.address     = ip.value();
        endpoint.version     = version;
        endpoint.primary     = json.contains("primary") && json["primary"].is_boolean() && json["primary"].get<bool>();
        endpoint.enabled     = !json.contains("enabled") || !json["enabled"].is_boolean() || json["enabled"].get<bool>();
        if (json.contains("metadata") && !json["metadata"].is_null())
        {
            endpoint.metadata_json = json["metadata"].is_string() ? json["metadata"].get<::std::string>() : json["metadata"].dump();
        }

        if (json.contains("endpoints") && json["endpoints"].is_array())
        {
            for (const auto& item : json["endpoints"])
            {
                if (!item.is_object())
                {
                    return ::std::unexpected(makeFailure(CatalogError::PROTOCOL_ERROR, "endpoint must be an object"));
                }
                Result<::std::string> name { requiredString(item, "name") };
                if (!name.has_value())
                {
                    return ::std::unexpected(name.error());
                }
                Result<::std::string> protocol { requiredString(item, "protocol") };
                if (!protocol.has_value())
                {
                    return ::std::unexpected(protocol.error());
                }
                if (item.contains("port") && !item["port"].is_number())
                {
                    return ::std::unexpected(makeFailure(CatalogError::PROTOCOL_ERROR, "invalid type: port"));
                }
                ExposedPort port {};
                port.name     = name.value();
                port.protocol = protocol.value();
                port.port     = item.value("port", 0);
                port.url      = item.value("url", ::std::string {});
                ::std::string endpoint_ip { item.value("address", ::std::string {}) };
                if (endpoint_ip.empty())
                {
                    endpoint_ip = item.value("ip", ::std::string {});
                }
                if (endpoint_ip.empty())
                {
                    endpoint_ip = ip.value();
                }
                port.ip   = endpoint_ip;
                port.path = item.value("path", ::std::string {});
                endpoint.exposed_ports.push_back(::std::move(port));
            }
        }
        else if (json.contains("port") && json["port"].is_number())
        {
            ExposedPort port {};
            port.name     = "default";
            port.protocol = "tcp";
            port.port     = json["port"].get<int>();
            port.ip       = ip.value();
            endpoint.exposed_ports.push_back(::std::move(port));
        }
        return endpoint;
    }

    [[nodiscard]] Result<ConfigDocument> ServiceGateway::parseConfig(const ::nlohmann::json& json)
    {
        if (!json.is_object())
        {
            return ::std::unexpected(makeFailure(CatalogError::PROTOCOL_ERROR, "config must be an object"));
        }
        ConfigDocument        document {};
        Result<::std::string> namespace_name { requiredString(json, "namespace") };
        if (!namespace_name.has_value())
        {
            return ::std::unexpected(namespace_name.error());
        }
        Result<::std::string> group_name { requiredString(json, "group") };
        if (!group_name.has_value())
        {
            return ::std::unexpected(group_name.error());
        }
        Result<::std::string> data_id { requiredString(json, "dataId") };
        if (!data_id.has_value())
        {
            return ::std::unexpected(data_id.error());
        }
        Result<::std::string> content { optionalString(json, "content") };
        if (!content.has_value())
        {
            return ::std::unexpected(content.error());
        }
        Result<::std::string> format { optionalString(json, "format") };
        if (!format.has_value())
        {
            return ::std::unexpected(format.error());
        }
        ::std::string version {};
        if (json.contains("version") && !json["version"].is_null())
        {
            if (json["version"].is_string())
            {
                version = json["version"].get<::std::string>();
            }
            else if (json["version"].is_number_integer() || json["version"].is_number_unsigned())
            {
                version = ::std::to_string(json["version"].get<long long>());
            }
            else
            {
                return ::std::unexpected(makeFailure(CatalogError::PROTOCOL_ERROR, "invalid type: version"));
            }
        }
        Result<::std::string> updated_at { optionalString(json, "updatedAt") };
        if (!updated_at.has_value())
        {
            return ::std::unexpected(updated_at.error());
        }
        document.key.namespace_name = namespace_name.value();
        document.key.group_name     = group_name.value();
        document.key.data_id        = data_id.value();
        document.content            = content.value();
        document.format             = format.value();
        document.version            = version;
        document.updated_at         = updated_at.value();
        return document;
    }

    [[nodiscard]] Result<ServiceStatus> ServiceGateway::parseServiceStatus(const ::nlohmann::json& json)
    {
        if (!json.is_object())
        {
            return ::std::unexpected(makeFailure(CatalogError::PROTOCOL_ERROR, "status must be an object"));
        }
        ServiceStatus status {};
        status.overall_status = json.value("overallStatus", ::std::string {});
        status.message        = json.value("message", ::std::string {});
        if (json.contains("components") && json["components"].is_object())
        {
            for (auto it { json["components"].begin() }; it != json["components"].end(); ++it)
            {
                const ::nlohmann::json& component { it.value() };
                if (!component.is_object())
                {
                    return ::std::unexpected(makeFailure(CatalogError::PROTOCOL_ERROR, "component must be an object: " + it.key()));
                }
                ServiceComponentStatus comp {};
                comp.name    = it.key();
                comp.status  = component.value("status", ::std::string {});
                comp.code    = component.value("code", ::std::string {});
                comp.message = component.value("message", ::std::string {});
                status.components.push_back(::std::move(comp));
            }
        }
        // 对齐 java "UP".equalsIgnoreCase(overall): 大小写不敏感
        status.healthy = asciiUpperEquals(status.overall_status, "UP");
        return status;
    }

    [[nodiscard]] Result<ServiceSummary> ServiceGateway::parseServiceSummary(const ::nlohmann::json& json)
    {
        if (!json.is_object() || !json.contains("service") || !json["service"].is_object())
        {
            return ::std::unexpected(makeFailure(CatalogError::PROTOCOL_ERROR, "service must be an object"));
        }
        const ::nlohmann::json& service { json["service"] };
        if (!service.contains("key") || !service["key"].is_object())
        {
            return ::std::unexpected(makeFailure(CatalogError::PROTOCOL_ERROR, "service.key must be an object"));
        }
        const ::nlohmann::json& key { service["key"] };
        Result<::std::string>   ns { optionalString(key, "namespace") };
        if (!ns.has_value())
        {
            return ::std::unexpected(ns.error());
        }
        Result<::std::string> group { optionalString(key, "group") };
        if (!group.has_value())
        {
            return ::std::unexpected(group.error());
        }
        Result<::std::string> service_id { requiredString(key, "serviceId") };
        if (!service_id.has_value())
        {
            return ::std::unexpected(service_id.error());
        }
        Result<::std::string> service_name { optionalString(service, "serviceName") };
        if (!service_name.has_value())
        {
            return ::std::unexpected(service_name.error());
        }
        Result<::std::string> source { optionalString(service, "source") };
        if (!source.has_value())
        {
            return ::std::unexpected(source.error());
        }
        ServiceSummary summary {};
        summary.service.key.namespace_name = ns.value();
        summary.service.key.group_name     = group.value();
        summary.service.key.service_id     = service_id.value();
        summary.service.service_name       = service_name.value();
        summary.service.source             = source.value();
        summary.total_instances            = json.value("totalInstances", 0ll);
        summary.healthy_instances          = json.value("healthyInstances", 0ll);
        summary.runtime_status             = json.value("runtimeStatus", ::std::string {});
        return summary;
    }

    [[nodiscard]] Result<CatalogServerInfo> ServiceGateway::parseCatalogServerInfo(const ::nlohmann::json& json)
    {
        if (!json.is_object())
        {
            return ::std::unexpected(makeFailure(CatalogError::PROTOCOL_ERROR, "catalog server info must be an object"));
        }
        Result<::std::string> ip { requiredString(json, "ip") };
        if (!ip.has_value())
        {
            return ::std::unexpected(ip.error());
        }
        Result<::std::string> node_id { requiredString(json, "nodeId") };
        if (!node_id.has_value())
        {
            return ::std::unexpected(node_id.error());
        }
        Result<::std::string> node_name { requiredString(json, "nodeName") };
        if (!node_name.has_value())
        {
            return ::std::unexpected(node_name.error());
        }
        // 组播字段为占位信息 (当前无消费者); 部分服务端版本不返回 (只有 multicastPort),
        // 缺失时置空, 不阻断 ip/nodeId/nodeName 的获取 (WebSocket 直连等依赖 ip 字段)。
        ::std::string multicast_ip {};
        if (json.contains("multicastIp") && !json["multicastIp"].is_null())
        {
            const ::nlohmann::json& multicast_node { json["multicastIp"] };
            if (!multicast_node.is_string() && !multicast_node.is_number())
            {
                return ::std::unexpected(makeFailure(CatalogError::PROTOCOL_ERROR, "invalid type: multicastIp"));
            }
            multicast_ip = multicast_node.is_string() ? multicast_node.get<::std::string>() : ::std::to_string(multicast_node.get<long long>());
        }
        ::std::string multicast_address {};
        if (json.contains("multicastAddress") && !json["multicastAddress"].is_null())
        {
            Result<::std::string> address { requiredString(json, "multicastAddress") };
            if (!address.has_value())
            {
                return ::std::unexpected(address.error());
            }
            multicast_address = address.value();
        }
        CatalogServerInfo info {};
        info.ip                = ip.value();
        info.node_id           = node_id.value();
        info.node_name         = node_name.value();
        info.multicast_ip      = ::std::move(multicast_ip);
        info.multicast_address = ::std::move(multicast_address);
        return info;
    }

    [[nodiscard]] Result<::std::string> ServiceGateway::requiredString(const ::nlohmann::json& json, const ::std::string& field)
    {
        if (!json.contains(field) || json[field].is_null() || !json[field].is_string())
        {
            return ::std::unexpected(makeFailure(CatalogError::PROTOCOL_ERROR, "missing or invalid field: " + field));
        }
        return json[field].get<::std::string>();
    }

    [[nodiscard]] Result<::std::string> ServiceGateway::optionalString(const ::nlohmann::json& json, const ::std::string& field)
    {
        if (!json.contains(field) || json[field].is_null())
        {
            return ::std::string {};
        }
        if (!json[field].is_string())
        {
            return ::std::unexpected(makeFailure(CatalogError::PROTOCOL_ERROR, "invalid type: " + field));
        }
        return json[field].get<::std::string>();
    }

    [[nodiscard]] Result<void> ServiceGateway::validateDataIds(const ::std::vector<::std::string>& ids)
    {
        if (ids.empty())
        {
            return ::std::unexpected(makeFailure(CatalogError::INVALID_ARGUMENT, "data_ids is empty"));
        }
        ::std::set<::std::string> distinct {};
        for (const auto& id : ids)
        {
            if (id.empty())
            {
                return ::std::unexpected(makeFailure(CatalogError::INVALID_ARGUMENT, "data_ids contains an empty value"));
            }
            if (!distinct.insert(id).second)
            {
                return ::std::unexpected(makeFailure(CatalogError::INVALID_ARGUMENT, "data_ids contains a duplicate value: " + id));
            }
        }
        return {};
    }

    [[nodiscard]] Result<void> ServiceGateway::validateConfigUploadRequest(const ConfigUploadRequest& request)
    {
        if (request.key.data_id.empty())
        {
            return ::std::unexpected(makeFailure(CatalogError::INVALID_ARGUMENT, "data_id is empty"));
        }
        if (request.content.empty())
        {
            return ::std::unexpected(makeFailure(CatalogError::INVALID_ARGUMENT, "content is empty"));
        }
        if (request.format.empty())
        {
            return ::std::unexpected(makeFailure(CatalogError::INVALID_ARGUMENT, "format is empty"));
        }
        for (const char ch : request.format)
        {
            const unsigned char uch { static_cast<unsigned char>(ch) };
            if (uch <= 0X20 || uch == 0X7f)
            {
                return ::std::unexpected(makeFailure(CatalogError::INVALID_ARGUMENT, "format contains whitespace or control characters"));
            }
        }
        return {};
    }

    [[nodiscard]] CatalogFailure ServiceGateway::remapNotFound(CatalogFailure failure, CatalogError replacement)
    {
        if (failure.http_status == 404)
        {
            return failureWithRetryable(failureWithCode(failure, replacement), false);
        }
        return failure;
    }
} // namespace plane::catalog::internal

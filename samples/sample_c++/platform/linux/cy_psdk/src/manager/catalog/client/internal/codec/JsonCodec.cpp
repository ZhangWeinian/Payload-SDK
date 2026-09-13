// cy_psdk/manager/catalog/client/internal/codec/JsonCodec.cpp

#include "manager/catalog/client/internal/codec/JsonCodec.h"

#include <set>

#include "manager/catalog/client/CatalogError.h"
#include "manager/catalog/client/CatalogFailure.h"
#include "manager/catalog/client/internal/util/TextUtil.h"

#include "define.h"

namespace plane::catalog::internal
{
    namespace JsonCodec
    {
        namespace
        {
            [[nodiscard]] const ::std::set<::std::string>& validStatuses(void)
            {
                static const ::std::set<::std::string> kValid { "UP", "DEGRADED", "DOWN", "UNKNOWN" };
                return kValid;
            }

            // 码点数近似按字节数 (ASCII 协议字段), 与服务端一致按 UTF-8 码点判断 <=128
            [[nodiscard]] bool exceeds128CodePoints(const ::std::string& value)
            {
                ::std::size_t code_points { 0 };
                for (const char raw_ch : value)
                {
                    const unsigned char ch { static_cast<unsigned char>(raw_ch) };
                    if ((ch & 0Xc0u) != 0X80)
                    {
                        ++code_points;
                    }
                }
                return code_points > 128;
            }
        } // namespace

        Result<::std::string> normalizeVersion(const ::std::string& raw)
        {
            // 对齐 java String.trim(): 仅去除首尾空白 (<= 0x20), 内部字符原样保留
            ::std::string value { trimAsciiWhitespaceCopy(raw) };
            if (value.empty())
            {
                return ::std::unexpected(makeFailure(CatalogError::INVALID_ARGUMENT, "version is empty"));
            }
            if (exceeds128CodePoints(value))
            {
                return ::std::unexpected(makeFailure(CatalogError::INVALID_ARGUMENT, "version exceeds 128 characters"));
            }
            return value;
        }

        Result<::nlohmann::json> registrationToJson(const ServiceRegistration& registration, const ::std::string& instance_address)
        {
            if (registration.service_id.empty())
            {
                return ::std::unexpected(makeFailure(CatalogError::INVALID_ARGUMENT, "service_id is empty"));
            }
            if (registration.service_name.empty())
            {
                return ::std::unexpected(makeFailure(CatalogError::INVALID_ARGUMENT, "service_name is empty"));
            }
            // 注: 允许空 exposed_ports (机载端当前无对外业务端口); 服务端以 HTTP 来源 IP 绑定实例。
            Result<::std::string> version { normalizeVersion(registration.version) };
            if (!version.has_value())
            {
                return ::std::unexpected(version.error());
            }

            ::nlohmann::json root;
            root["cluster"]            = "DEFAULT";
            root["weight"]             = 1.0;
            root["enabled"]            = true;
            root["ephemeral"]          = true;
            root["serviceName"]        = registration.service_name;
            root["version"]            = version.value();

            ::nlohmann::json endpoints = ::nlohmann::json::array();
            for (const auto& port : registration.exposed_ports)
            {
                ::nlohmann::json endpoint;
                endpoint["name"]     = port.name;
                endpoint["protocol"] = port.protocol;
                if (!instance_address.empty())
                {
                    endpoint["address"] = instance_address;
                }
                endpoint["port"] = port.port;
                endpoint["path"] = "";
                if (!port.url.empty())
                {
                    endpoint["url"] = port.url;
                }
                endpoint["metadata"] = ::nlohmann::json::object();
                endpoints.push_back(::std::move(endpoint));
            }
            root["endpoints"] = ::std::move(endpoints);

            if (registration.metadata_json.empty())
            {
                root["metadata"] = ::nlohmann::json::object();
            }
            else
            {
                try
                {
                    ::nlohmann::json metadata = ::nlohmann::json::parse(registration.metadata_json);
                    if (!metadata.is_object())
                    {
                        return ::std::unexpected(makeFailure(CatalogError::INVALID_ARGUMENT, "metadata must be an object"));
                    }
                    for (auto it { metadata.begin() }; it != metadata.end(); ++it)
                    {
                        if (!it.value().is_string())
                        {
                            return ::std::unexpected(makeFailure(CatalogError::INVALID_ARGUMENT, "metadata values must be strings"));
                        }
                    }
                    root["metadata"] = ::std::move(metadata);
                }
                catch (const ::std::exception& ex)
                {
                    return ::std::unexpected(makeFailure(CatalogError::INVALID_ARGUMENT, ex.what()));
                }
            }
            return root;
        }

        Result<::nlohmann::json> statusToJson(const ServiceStatus& status)
        {
            const ::std::string overall { status.overall_status.empty() ? (status.healthy ? "UP" : "DOWN") : status.overall_status };
            if (validStatuses().find(overall) == validStatuses().end())
            {
                return ::std::unexpected(makeFailure(CatalogError::PROTOCOL_ERROR, "invalid overall status"));
            }

            ::nlohmann::json root;
            root["overallStatus"] = overall;
            root["message"]       = status.message;

            auto addComponent     = [](::nlohmann::json&                               components,
                                       const ::std::string&                            name,
                                       const ::std::string&                            component_status,
                                       const ::std::string&                            code,
                                       const ::std::string&                            message,
                                       const ::std::map<::std::string, ::std::string>& details)
            {
                ::nlohmann::json component;
                component["status"]           = component_status;
                component["code"]             = code;
                component["message"]          = message;
                ::nlohmann::json details_node = ::nlohmann::json::object();
                for (const auto& [key, value] : details)
                {
                    details_node[key] = value;
                }
                component["details"] = ::std::move(details_node);
                components[name]     = ::std::move(component);
            };

            ::nlohmann::json components = ::nlohmann::json::object();
            if (status.components.empty())
            {
                addComponent(components, "application", status.healthy ? "UP" : "DOWN", status.code, status.message, status.details);
            }
            else
            {
                ::std::set<::std::string> names {};
                for (const auto& component : status.components)
                {
                    if (component.name.empty())
                    {
                        return ::std::unexpected(makeFailure(CatalogError::PROTOCOL_ERROR, "component name is empty"));
                    }
                    if (validStatuses().find(component.status) == validStatuses().end())
                    {
                        return ::std::unexpected(makeFailure(CatalogError::PROTOCOL_ERROR, "invalid component status"));
                    }
                    if (!names.insert(component.name).second)
                    {
                        return ::std::unexpected(makeFailure(CatalogError::PROTOCOL_ERROR, "duplicate component name"));
                    }
                    addComponent(components, component.name, component.status, component.code, component.message, component.details);
                }
            }
            root["components"] = ::std::move(components);
            return root;
        }
    } // namespace JsonCodec
} // namespace plane::catalog::internal

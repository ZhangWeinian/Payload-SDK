// cy_psdk/manager/catalog/client/internal/service/ServiceGateway.h
//
// 目录 HTTP API 网关 (对齐 java ServiceGateway): 注册/心跳/状态/服务解析/配置等。

#pragma once

#include <nlohmann/json.hpp>
#include <memory>
#include <string>
#include <vector>

#include "manager/catalog/client/CatalogModels.h"
#include "manager/catalog/client/internal/transport/CatalogTransport.h"
#include "manager/catalog/client/internal/transport/HttpTransport.h"
#include "manager/catalog/client/Result.h"

#include "define.h"

namespace plane::catalog::internal
{
    class ServiceGateway
    {
    public:
        // http 为空时使用默认 CppHttpTransport
        ServiceGateway(::std::string catalog_url, ::std::unique_ptr<HttpTransport> http, ::std::chrono::milliseconds timeout);
        ~ServiceGateway(void)                                        = default;

        ServiceGateway(const ServiceGateway&)                        = delete;
        ServiceGateway&             operator=(const ServiceGateway&) = delete;

        [[nodiscard]] ::std::string catalogUrl(void) const
        {
            return this->transport_.catalogUrl();
        }

        void setCatalogUrl(::std::string value)
        {
            this->transport_.setCatalogUrl(::std::move(value));
        }

        void setTimeout(::std::chrono::milliseconds value)
        {
            this->transport_.setTimeout(value);
        }

        void setRemoteAllowed(bool value)
        {
            this->transport_.setRemoteAllowed(value);
        }

        void setConflict(bool value)
        {
            this->transport_.setConflict(value);
        }

        void setStopping(bool value)
        {
            this->transport_.setStopping(value);
        }

        // 注册实例, 成功返回实例 ID (幂等: 409 且服务端判幂等时也视为成功)
        [[nodiscard]] Result<::std::string> registerInstance(const ServiceRegistration& registration);
        [[nodiscard]] Result<void>          heartbeat(const ServiceRegistration& registration, const ::std::string& instance_id);
        [[nodiscard]] Result<void>
            reportStatus(const ServiceRegistration& registration, const ::std::string& instance_id, const ServiceStatus& status);
        [[nodiscard]] Result<void>            deleteInstance(const ServiceRegistration& registration, const ::std::string& instance_id);

        [[nodiscard]] Result<ResolvedService> resolve(const ServiceQuery& query);
        [[nodiscard]] Result<ServicePage>
            listServices(const ::std::string& namespace_name, const ::std::string& service_name, int page, int page_size);
        [[nodiscard]] Result<ServiceStatus> getInstanceStatus(
            const ::std::string& namespace_name,
            const ::std::string& group_name,
            const ::std::string& service_id,
            const ::std::string& instance_id
        );

        [[nodiscard]] Result<::std::string> getLocalIp(
            const ::std::string& namespace_name,
            const ::std::string& group_name,
            const ::std::string& service_id,
            bool                 allow_legacy_fallback
        );
        [[nodiscard]] Result<CatalogServerInfo>             getCatalogServerInfo(void);

        [[nodiscard]] Result<::std::vector<ConfigDocument>> getConfigs(const ConfigQuery& query);
        [[nodiscard]] Result<ConfigDocument>                getConfig(const ConfigKey& key);
        [[nodiscard]] Result<ConfigDocument>                putConfig(const ConfigUploadRequest& request);

    private:
        // JSON 字段解析辅助 (服务端响应字段为 camelCase)
        static Result<::std::string> requiredString(const ::nlohmann::json& json, const ::std::string& field);
        static Result<::std::string> optionalString(const ::nlohmann::json& json, const ::std::string& field);
        static Result<void>          validateDataIds(const ::std::vector<::std::string>& ids);
        static Result<void>          validateConfigUploadRequest(const ConfigUploadRequest& request);

        static ::std::string
            instanceCollection(const ::std::string& namespace_name, const ::std::string& group_name, const ::std::string& service_id);
        static ::std::string             scopeValue(const ::std::string& value, const ::std::string& fallback);

        static Result<ServiceEndpoint>   parseEndpoint(const ::nlohmann::json& json, bool& healthy);
        static Result<ConfigDocument>    parseConfig(const ::nlohmann::json& json);
        static Result<ServiceStatus>     parseServiceStatus(const ::nlohmann::json& json);
        static Result<ServiceSummary>    parseServiceSummary(const ::nlohmann::json& json);
        static Result<CatalogServerInfo> parseCatalogServerInfo(const ::nlohmann::json& json);

        static CatalogFailure            remapNotFound(CatalogFailure failure, CatalogError replacement);

        CatalogTransport                 transport_;
    };
} // namespace plane::catalog::internal

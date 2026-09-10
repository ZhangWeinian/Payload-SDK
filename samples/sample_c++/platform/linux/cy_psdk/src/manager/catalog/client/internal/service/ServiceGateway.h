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
		ServiceGateway(_STD string catalog_url, _STD unique_ptr<HttpTransport> http, _STD_CHRONO milliseconds timeout);
		~ServiceGateway(void)									= default;

		ServiceGateway(const ServiceGateway&)					= delete;
		ServiceGateway&		   operator=(const ServiceGateway&) = delete;

		_NODISCARD _STD string catalogUrl(void) const
		{
			return this->transport_.catalogUrl();
		}

		void setCatalogUrl(_STD string value)
		{
			this->transport_.setCatalogUrl(_STD move(value));
		}

		void setTimeout(_STD_CHRONO milliseconds value)
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
		_NODISCARD Result<_STD string> registerInstance(const ServiceRegistration& registration);
		_NODISCARD Result<void> heartbeat(const ServiceRegistration& registration, const _STD string& instance_id);
		_NODISCARD				Result<void>
				   reportStatus(const ServiceRegistration& registration, const _STD string& instance_id, const ServiceStatus& status);
		_NODISCARD Result<void> deleteInstance(const ServiceRegistration& registration, const _STD string& instance_id);

		_NODISCARD Result<ResolvedService> resolve(const ServiceQuery& query);
		_NODISCARD Result<ServicePage> listServices(const _STD string& namespace_name, const _STD string& service_name, int page, int page_size);
		_NODISCARD Result<ServiceStatus> getInstanceStatus(
			const _STD string& namespace_name,
			const _STD string& group_name,
			const _STD string& service_id,
			const _STD string& instance_id
		);

		_NODISCARD Result<_STD string> getLocalIp(
			const _STD string& namespace_name,
			const _STD string& group_name,
			const _STD string& service_id,
			bool			   allow_legacy_fallback
		);
		_NODISCARD Result<CatalogServerInfo> getCatalogServerInfo(void);

		_NODISCARD Result<_STD vector<ConfigDocument>> getConfigs(const ConfigQuery& query);
		_NODISCARD Result<ConfigDocument> getConfig(const ConfigKey& key);
		_NODISCARD Result<ConfigDocument> putConfig(const ConfigUploadRequest& request);

	private:
		// JSON 字段解析辅助 (服务端响应字段为 camelCase)
		static Result<_STD string> requiredString(const _NLOHMANN_JSON json& json, const _STD string& field);
		static Result<_STD string> optionalString(const _NLOHMANN_JSON json& json, const _STD string& field);
		static Result<void>		   validateDataIds(const _STD vector<_STD string>& ids);
		static Result<void>		   validateConfigUploadRequest(const ConfigUploadRequest& request);

		static _STD string instanceCollection(const _STD string& namespace_name, const _STD string& group_name, const _STD string& service_id);
		static _STD string scopeValue(const _STD string& value, const _STD string& fallback);

		static Result<ServiceEndpoint>	 parseEndpoint(const _NLOHMANN_JSON json& json, bool& healthy);
		static Result<ConfigDocument>	 parseConfig(const _NLOHMANN_JSON json& json);
		static Result<ServiceStatus>	 parseServiceStatus(const _NLOHMANN_JSON json& json);
		static Result<ServiceSummary>	 parseServiceSummary(const _NLOHMANN_JSON json& json);
		static Result<CatalogServerInfo> parseCatalogServerInfo(const _NLOHMANN_JSON json& json);

		static CatalogFailure			 remapNotFound(CatalogFailure failure, CatalogError replacement);

		CatalogTransport				 transport_;
	};
} // namespace plane::catalog::internal

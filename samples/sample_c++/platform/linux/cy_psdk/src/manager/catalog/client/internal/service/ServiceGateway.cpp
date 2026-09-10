// cy_psdk/manager/catalog/client/internal/service/ServiceGateway.cpp

#include "manager/catalog/client/internal/service/ServiceGateway.h"

#include <arpa/inet.h>
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
		constexpr const char* kRegistry		= "/api/registry/services";
		constexpr const char* kConfigs		= "/api/configs";
		constexpr const char* kConfigsBatch = "/api/configs/batch";
		constexpr const char* kLocalIp		= "/api/registry/services/local-ip";
		constexpr const char* kUdpConfig	= "/api/udp-config";

		_NODISCARD Result<_STD string> invalidString(const _STD string& message)
		{
			return Result<_STD string>::failure(makeFailure(CatalogError::INVALID_ARGUMENT, message));
		}

		_NODISCARD bool isValidIpv4(const _STD string& value)
		{
			in_addr address {};
			return ::inet_pton(AF_INET, value.c_str(), &address) == 1;
		}

		// 大小写不敏感相等 (期望 upper 为大写形态; ASCII 协议字段)
		_NODISCARD bool asciiUpperEquals(_STD string_view value, _STD string_view upper) noexcept
		{
			if (value.size() != upper.size())
			{
				return false;
			}
			for (_STD size_t index { 0 }; index < value.size(); ++index)
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
		_NODISCARD _STD string encodeComponent(const _STD string& value)
		{
			_STD string result {};
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
					result.push_back(kHex[(ch >> 4) & 0X0f]);
					result.push_back(kHex[ch & 0X0f]);
				}
			}
			return result;
		}
	} // namespace

	ServiceGateway::ServiceGateway(_STD string catalog_url, _STD unique_ptr<HttpTransport> http, _STD_CHRONO milliseconds timeout):
		transport_(_STD move(catalog_url), _STD move(http))
	{
		this->transport_.setTimeout(timeout);
	}

	_NODISCARD _STD string ServiceGateway::scopeValue(const _STD string& value, const _STD string& fallback)
	{
		return value.empty() ? fallback : value;
	}

	_NODISCARD _STD string ServiceGateway::
		instanceCollection(const _STD string& namespace_name, const _STD string& group_name, const _STD string& service_id)
	{
		return _STD string { kRegistry } + "/" + encodeComponent(scopeValue(namespace_name, "public")) + "/" +
			   encodeComponent(scopeValue(group_name, "DEFAULT_GROUP")) + "/" + encodeComponent(service_id) + "/instances";
	}

	_NODISCARD Result<_STD string> ServiceGateway::registerInstance(const ServiceRegistration& registration)
	{
		Result<_NLOHMANN_JSON json> body { JsonCodec::registrationToJson(registration, "") };
		if (!body.isOk())
		{
			return Result<_STD string>::failure(body.error());
		}
		const _STD string			path { instanceCollection(registration.namespace_name, registration.group_name, registration.service_id) };
		Result<_NLOHMANN_JSON json> response { this->transport_.postJson(path, body.value().dump(), true) };
		if (!response.isOk())
		{
			return Result<_STD string>::failure(remapNotFound(response.error(), CatalogError::INSTANCE_NOT_FOUND));
		}
		const _NLOHMANN_JSON json& json { response.value() };
		if (!json.contains("id") || !json["id"].is_string())
		{
			return Result<_STD string>::failure(makeFailure(CatalogError::PROTOCOL_ERROR, "missing or invalid field: id"));
		}
		return Result<_STD string>::success(json["id"].get<_STD string>());
	}

	_NODISCARD Result<void> ServiceGateway::heartbeat(const ServiceRegistration& registration, const _STD string& instance_id)
	{
		const _STD string path { instanceCollection(registration.namespace_name, registration.group_name, registration.service_id) + "/" +
								 encodeComponent(instance_id) + "/heartbeat" };
		Result<_NLOHMANN_JSON json> response { this->transport_.postJson(path, "{}") };
		if (!response.isOk())
		{
			return Result<void>::failure(remapNotFound(response.error(), CatalogError::INSTANCE_NOT_FOUND));
		}
		const _NLOHMANN_JSON json& json { response.value() };
		if (!json.is_object())
		{
			return Result<void>::failure(makeFailure(CatalogError::PROTOCOL_ERROR, "heartbeat response must be an object"));
		}
		if (json.contains("id") && !json["id"].is_null() && !json["id"].is_string())
		{
			return Result<void>::failure(makeFailure(CatalogError::PROTOCOL_ERROR, "invalid type: id"));
		}
		return Result<void>::success();
	}

	_NODISCARD Result<void>
			   ServiceGateway::reportStatus(const ServiceRegistration& registration, const _STD string& instance_id, const ServiceStatus& status)
	{
		Result<_NLOHMANN_JSON json> body { JsonCodec::statusToJson(status) };
		if (!body.isOk())
		{
			return Result<void>::failure(body.error());
		}
		const _STD string path { instanceCollection(registration.namespace_name, registration.group_name, registration.service_id) + "/" +
								 encodeComponent(instance_id) + "/status" };
		Result<void>	  result { this->transport_.putVoid(path, body.value().dump()) };
		if (!result.isOk())
		{
			return Result<void>::failure(remapNotFound(result.error(), CatalogError::INSTANCE_NOT_FOUND));
		}
		return Result<void>::success();
	}

	_NODISCARD Result<void> ServiceGateway::deleteInstance(const ServiceRegistration& registration, const _STD string& instance_id)
	{
		const _STD string path { instanceCollection(registration.namespace_name, registration.group_name, registration.service_id) + "/" +
								 encodeComponent(instance_id) };
		return this->transport_.del(path);
	}

	_NODISCARD Result<ResolvedService> ServiceGateway::resolve(const ServiceQuery& query)
	{
		const _STD string service_id { query.service_id.empty() ? query.service_name : query.service_id };
		if (service_id.empty())
		{
			return Result<ResolvedService>::failure(makeFailure(CatalogError::INVALID_ARGUMENT, "service_id is empty"));
		}
		Result<_NLOHMANN_JSON json> response {
			this->transport_.getJson(instanceCollection(query.namespace_name, query.group_name, service_id) + "?healthyOnly=true")
		};
		if (!response.isOk())
		{
			return Result<ResolvedService>::failure(remapNotFound(response.error(), CatalogError::SERVICE_NOT_FOUND));
		}
		const _NLOHMANN_JSON json& json { response.value() };
		if (!json.is_array())
		{
			return Result<ResolvedService>::failure(makeFailure(CatalogError::PROTOCOL_ERROR, "instances must be an array"));
		}
		ResolvedService resolved {};
		for (const auto& item : json)
		{
			bool					healthy { false };
			Result<ServiceEndpoint> parsed { parseEndpoint(item, healthy) };
			if (!parsed.isOk())
			{
				return Result<ResolvedService>::failure(parsed.error());
			}
			if (healthy && parsed.value().enabled)
			{
				resolved.endpoints.push_back(_STD move(parsed.value()));
			}
		}
		return Result<ResolvedService>::success(_STD move(resolved));
	}

	_NODISCARD Result<ServicePage>
			   ServiceGateway::listServices(const _STD string& namespace_name, const _STD string& service_name, int page, int page_size)
	{
		const int					safe_page { page < 1 ? 1 : page };
		const int					safe_page_size { page_size < 1 ? 20 : page_size };
		const _STD string			path { _STD string { kRegistry } + "?namespace=" + encodeComponent(scopeValue(namespace_name, "public")) +
										   "&serviceName=" + encodeComponent(service_name) + "&page=" + _STD to_string(safe_page) +
										   "&pageSize=" + _STD												 to_string(safe_page_size) };
		Result<_NLOHMANN_JSON json> response { this->transport_.getJson(path) };
		if (!response.isOk())
		{
			return Result<ServicePage>::failure(response.error());
		}
		const _NLOHMANN_JSON json& json { response.value() };
		if (!json.is_object() || !json.contains("items") || !json["items"].is_array())
		{
			return Result<ServicePage>::failure(makeFailure(CatalogError::PROTOCOL_ERROR, "service page items must be an array"));
		}
		ServicePage result {};
		for (const auto& item : json["items"])
		{
			Result<ServiceSummary> parsed { parseServiceSummary(item) };
			if (!parsed.isOk())
			{
				return Result<ServicePage>::failure(parsed.error());
			}
			result.items.push_back(_STD move(parsed.value()));
		}
		result.total_elements = json.value("totalElements", static_cast<long long>(result.items.size()));
		result.page			  = json.value("page", safe_page);
		result.page_size	  = json.value("pageSize", safe_page_size);
		return Result<ServicePage>::success(_STD move(result));
	}

	_NODISCARD Result<ServiceStatus> ServiceGateway::getInstanceStatus(
		const _STD string& namespace_name,
		const _STD string& group_name,
		const _STD string& service_id,
		const _STD string& instance_id
	)
	{
		if (instance_id.empty())
		{
			return Result<ServiceStatus>::failure(makeFailure(CatalogError::INVALID_ARGUMENT, "instance_id is empty"));
		}
		const _STD string path { instanceCollection(namespace_name, group_name, service_id) + "/" + encodeComponent(instance_id) + "/status" };
		Result<_NLOHMANN_JSON json> response { this->transport_.getJson(path) };
		if (!response.isOk())
		{
			return Result<ServiceStatus>::failure(remapNotFound(response.error(), CatalogError::INSTANCE_NOT_FOUND));
		}
		return parseServiceStatus(response.value());
	}

	_NODISCARD Result<_STD string> ServiceGateway::
		getLocalIp(const _STD string& namespace_name, const _STD string& group_name, const _STD string& service_id, bool allow_legacy_fallback)
	{
		Result<_NLOHMANN_JSON json> response { this->transport_.getJson(kLocalIp) };
		if (!response.isOk() && response.error().http_status == 404 && allow_legacy_fallback)
		{
			const _STD string legacy_path { _STD string { kRegistry } + "/" + encodeComponent(scopeValue(namespace_name, "public")) + "/" +
											encodeComponent(scopeValue(group_name, "DEFAULT_GROUP")) + "/" + encodeComponent(service_id) +
											"/local-ip" };
			response = this->transport_.getJson(legacy_path);
			if (!response.isOk())
			{
				return Result<_STD string>::failure(remapNotFound(response.error(), CatalogError::INSTANCE_NOT_FOUND));
			}
		}
		if (!response.isOk())
		{
			return Result<_STD string>::failure(response.error());
		}
		const _NLOHMANN_JSON json& json { response.value() };
		if (!json.is_object())
		{
			return Result<_STD string>::failure(makeFailure(CatalogError::PROTOCOL_ERROR, "local ip response must be an object"));
		}
		Result<_STD string> ip { requiredString(json, "ip") };
		if (!ip.isOk())
		{
			return ip;
		}
		if (!isValidIpv4(ip.value()))
		{
			return invalidString("invalid IPv4 address: " + ip.value());
		}
		return ip;
	}

	_NODISCARD Result<CatalogServerInfo> ServiceGateway::getCatalogServerInfo(void)
	{
		Result<_NLOHMANN_JSON json> response { this->transport_.getJson(kUdpConfig) };
		if (!response.isOk())
		{
			return Result<CatalogServerInfo>::failure(response.error());
		}
		return parseCatalogServerInfo(response.value());
	}

	_NODISCARD Result<_STD vector<ConfigDocument>> ServiceGateway::getConfigs(const ConfigQuery& query)
	{
		Result<void> validation { validateDataIds(query.data_ids) };
		if (!validation.isOk())
		{
			return Result<_STD vector<ConfigDocument>>::failure(validation.error());
		}
		_NLOHMANN_JSON json body;
		body["namespace"] = query.namespace_name;
		body["group"]	  = query.group_name;
		body["dataIds"]	  = _NLOHMANN_JSON json::array();
		for (const auto& id : query.data_ids)
		{
			body["dataIds"].push_back(id);
		}
		Result<_NLOHMANN_JSON json> response { this->transport_.postJson(kConfigsBatch, body.dump()) };
		if (!response.isOk())
		{
			CatalogFailure failure { remapNotFound(response.error(), CatalogError::CONFIG_NOT_FOUND) };
			if (failure.http_status == 400)
			{
				failure = failureWithCode(failure, CatalogError::INVALID_ARGUMENT);
			}
			return Result<_STD vector<ConfigDocument>>::failure(failure);
		}
		const _NLOHMANN_JSON json& json { response.value() };
		if (!json.is_array())
		{
			return Result<_STD vector<ConfigDocument>>::failure(makeFailure(CatalogError::PROTOCOL_ERROR, "configs must be an array"));
		}

		_STD set<_STD string> requested { query.data_ids.begin(), query.data_ids.end() };
		_STD map<_STD string, ConfigDocument> by_id {};
		for (const auto& item : json)
		{
			Result<ConfigDocument> parsed { parseConfig(item) };
			if (!parsed.isOk())
			{
				return Result<_STD vector<ConfigDocument>>::failure(parsed.error());
			}
			const ConfigDocument& document { parsed.value() };
			if (document.key.namespace_name != query.namespace_name || document.key.group_name != query.group_name)
			{
				return Result<_STD vector<ConfigDocument>>::
					failure(makeFailure(CatalogError::PROTOCOL_ERROR, "config scope does not match request: " + document.key.data_id));
			}
			if (requested.find(document.key.data_id) == requested.end())
			{
				return Result<_STD vector<ConfigDocument>>::
					failure(makeFailure(CatalogError::PROTOCOL_ERROR, "unexpected dataId in batch response: " + document.key.data_id));
			}
			if (by_id.find(document.key.data_id) != by_id.end())
			{
				return Result<_STD vector<ConfigDocument>>::
					failure(makeFailure(CatalogError::PROTOCOL_ERROR, "duplicate dataId in batch response: " + document.key.data_id));
			}
			by_id[document.key.data_id] = document;
		}
		_STD vector<ConfigDocument> ordered {};
		for (const auto& id : query.data_ids)
		{
			const auto it { by_id.find(id) };
			if (it == by_id.end())
			{
				return Result<_STD vector<ConfigDocument>>::
					failure(makeFailure(CatalogError::PROTOCOL_ERROR, "missing dataId in batch response: " + id));
			}
			ordered.push_back(it->second);
		}
		return Result<_STD vector<ConfigDocument>>::success(_STD move(ordered));
	}

	_NODISCARD Result<ConfigDocument> ServiceGateway::getConfig(const ConfigKey& key)
	{
		if (key.data_id.empty())
		{
			return Result<ConfigDocument>::failure(makeFailure(CatalogError::INVALID_ARGUMENT, "data_id is empty"));
		}
		const _STD string path { _STD string { kConfigs } + "/" + encodeComponent(key.namespace_name) + "/" + encodeComponent(key.group_name) +
								 "/" + encodeComponent(key.data_id) };
		Result<_NLOHMANN_JSON json> response { this->transport_.getJson(path) };
		if (!response.isOk())
		{
			return Result<ConfigDocument>::failure(remapNotFound(response.error(), CatalogError::CONFIG_NOT_FOUND));
		}
		return parseConfig(response.value());
	}

	_NODISCARD Result<ConfigDocument> ServiceGateway::putConfig(const ConfigUploadRequest& request)
	{
		Result<void> validation { validateConfigUploadRequest(request) };
		if (!validation.isOk())
		{
			return Result<ConfigDocument>::failure(validation.error());
		}
		const _STD string	path { _STD string { kConfigs } + "/" + encodeComponent(request.key.namespace_name) + "/" +
								   encodeComponent(request.key.group_name) + "/" + encodeComponent(request.key.data_id) };
		_NLOHMANN_JSON json body;
		body["content"] = request.content;
		body["format"]	= request.format;
		Result<_NLOHMANN_JSON json> response { this->transport_.putJson(path, body.dump()) };
		if (!response.isOk())
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
			return Result<ConfigDocument>::failure(failure);
		}
		Result<ConfigDocument> parsed { parseConfig(response.value()) };
		if (!parsed.isOk())
		{
			return parsed;
		}
		const ConfigDocument& saved { parsed.value() };
		if (saved.key.namespace_name != request.key.namespace_name || saved.key.group_name != request.key.group_name ||
			saved.key.data_id != request.key.data_id)
		{
			return Result<ConfigDocument>::failure(makeFailure(CatalogError::PROTOCOL_ERROR, "saved config key does not match upload request"));
		}
		if (saved.content != request.content || saved.format != request.format)
		{
			return Result<ConfigDocument>::
				failure(makeFailure(CatalogError::PROTOCOL_ERROR, "saved config content or format does not match upload request"));
		}
		if (saved.version.empty() || saved.updated_at.empty())
		{
			return Result<ConfigDocument>::failure(makeFailure(CatalogError::PROTOCOL_ERROR, "saved config is missing version or updatedAt"));
		}
		return parsed;
	}

	_NODISCARD Result<ServiceEndpoint> ServiceGateway::parseEndpoint(const _NLOHMANN_JSON json& json, bool& healthy)
	{
		healthy = false;
		if (!json.is_object())
		{
			return Result<ServiceEndpoint>::failure(makeFailure(CatalogError::PROTOCOL_ERROR, "instance must be an object"));
		}
		Result<_STD string> id { requiredString(json, "id") };
		if (!id.isOk())
		{
			return Result<ServiceEndpoint>::failure(id.error());
		}
		Result<_STD string> ip { requiredString(json, "ip") };
		if (!ip.isOk())
		{
			return Result<ServiceEndpoint>::failure(ip.error());
		}
		_STD string version {};
		if (json.contains("version") && !json["version"].is_null())
		{
			if (!json["version"].is_string())
			{
				return Result<ServiceEndpoint>::failure(makeFailure(CatalogError::PROTOCOL_ERROR, "invalid type: version"));
			}
			version = json["version"].get<_STD string>();
		}
		healthy = !json.contains("healthy") || !json["healthy"].is_boolean() || json["healthy"].get<bool>();

		ServiceEndpoint endpoint {};
		endpoint.instance_id = id.value();
		endpoint.address	 = ip.value();
		endpoint.version	 = version;
		endpoint.primary	 = json.contains("primary") && json["primary"].is_boolean() && json["primary"].get<bool>();
		endpoint.enabled	 = !json.contains("enabled") || !json["enabled"].is_boolean() || json["enabled"].get<bool>();
		if (json.contains("metadata") && !json["metadata"].is_null())
		{
			endpoint.metadata_json = json["metadata"].is_string() ? json["metadata"].get<_STD string>() : json["metadata"].dump();
		}

		if (json.contains("endpoints") && json["endpoints"].is_array())
		{
			for (const auto& item : json["endpoints"])
			{
				if (!item.is_object())
				{
					return Result<ServiceEndpoint>::failure(makeFailure(CatalogError::PROTOCOL_ERROR, "endpoint must be an object"));
				}
				Result<_STD string> name { requiredString(item, "name") };
				if (!name.isOk())
				{
					return Result<ServiceEndpoint>::failure(name.error());
				}
				Result<_STD string> protocol { requiredString(item, "protocol") };
				if (!protocol.isOk())
				{
					return Result<ServiceEndpoint>::failure(protocol.error());
				}
				if (item.contains("port") && !item["port"].is_number())
				{
					return Result<ServiceEndpoint>::failure(makeFailure(CatalogError::PROTOCOL_ERROR, "invalid type: port"));
				}
				ExposedPort port {};
				port.name	  = name.value();
				port.protocol = protocol.value();
				port.port	  = item.value("port", 0);
				port.url	  = item.value("url", _STD string {});
				_STD string endpoint_ip { item.value("address", _STD string {}) };
				if (endpoint_ip.empty())
				{
					endpoint_ip = item.value("ip", _STD string {});
				}
				if (endpoint_ip.empty())
				{
					endpoint_ip = ip.value();
				}
				port.ip	  = endpoint_ip;
				port.path = item.value("path", _STD string {});
				endpoint.exposed_ports.push_back(_STD move(port));
			}
		}
		else if (json.contains("port") && json["port"].is_number())
		{
			ExposedPort port {};
			port.name	  = "default";
			port.protocol = "tcp";
			port.port	  = json["port"].get<int>();
			port.ip		  = ip.value();
			endpoint.exposed_ports.push_back(_STD move(port));
		}
		return Result<ServiceEndpoint>::success(_STD move(endpoint));
	}

	_NODISCARD Result<ConfigDocument> ServiceGateway::parseConfig(const _NLOHMANN_JSON json& json)
	{
		if (!json.is_object())
		{
			return Result<ConfigDocument>::failure(makeFailure(CatalogError::PROTOCOL_ERROR, "config must be an object"));
		}
		ConfigDocument		document {};
		Result<_STD string> namespace_name { requiredString(json, "namespace") };
		if (!namespace_name.isOk())
		{
			return Result<ConfigDocument>::failure(namespace_name.error());
		}
		Result<_STD string> group_name { requiredString(json, "group") };
		if (!group_name.isOk())
		{
			return Result<ConfigDocument>::failure(group_name.error());
		}
		Result<_STD string> data_id { requiredString(json, "dataId") };
		if (!data_id.isOk())
		{
			return Result<ConfigDocument>::failure(data_id.error());
		}
		Result<_STD string> content { optionalString(json, "content") };
		if (!content.isOk())
		{
			return Result<ConfigDocument>::failure(content.error());
		}
		Result<_STD string> format { optionalString(json, "format") };
		if (!format.isOk())
		{
			return Result<ConfigDocument>::failure(format.error());
		}
		_STD string version {};
		if (json.contains("version") && !json["version"].is_null())
		{
			if (json["version"].is_string())
			{
				version = json["version"].get<_STD string>();
			}
			else if (json["version"].is_number_integer() || json["version"].is_number_unsigned())
			{
				version = _STD to_string(json["version"].get<long long>());
			}
			else
			{
				return Result<ConfigDocument>::failure(makeFailure(CatalogError::PROTOCOL_ERROR, "invalid type: version"));
			}
		}
		Result<_STD string> updated_at { optionalString(json, "updatedAt") };
		if (!updated_at.isOk())
		{
			return Result<ConfigDocument>::failure(updated_at.error());
		}
		document.key.namespace_name = namespace_name.value();
		document.key.group_name		= group_name.value();
		document.key.data_id		= data_id.value();
		document.content			= content.value();
		document.format				= format.value();
		document.version			= version;
		document.updated_at			= updated_at.value();
		return Result<ConfigDocument>::success(_STD move(document));
	}

	_NODISCARD Result<ServiceStatus> ServiceGateway::parseServiceStatus(const _NLOHMANN_JSON json& json)
	{
		if (!json.is_object())
		{
			return Result<ServiceStatus>::failure(makeFailure(CatalogError::PROTOCOL_ERROR, "status must be an object"));
		}
		ServiceStatus status {};
		status.overall_status = json.value("overallStatus", _STD string {});
		status.message		  = json.value("message", _STD string {});
		if (json.contains("components") && json["components"].is_object())
		{
			for (auto it { json["components"].begin() }; it != json["components"].end(); ++it)
			{
				const _NLOHMANN_JSON json& component { it.value() };
				if (!component.is_object())
				{
					return Result<ServiceStatus>::failure(makeFailure(CatalogError::PROTOCOL_ERROR, "component must be an object: " + it.key()));
				}
				ServiceComponentStatus comp {};
				comp.name	 = it.key();
				comp.status	 = component.value("status", _STD string {});
				comp.code	 = component.value("code", _STD string {});
				comp.message = component.value("message", _STD string {});
				status.components.push_back(_STD move(comp));
			}
		}
		// 对齐 java "UP".equalsIgnoreCase(overall): 大小写不敏感
		status.healthy = asciiUpperEquals(status.overall_status, "UP");
		return Result<ServiceStatus>::success(_STD move(status));
	}

	_NODISCARD Result<ServiceSummary> ServiceGateway::parseServiceSummary(const _NLOHMANN_JSON json& json)
	{
		if (!json.is_object() || !json.contains("service") || !json["service"].is_object())
		{
			return Result<ServiceSummary>::failure(makeFailure(CatalogError::PROTOCOL_ERROR, "service must be an object"));
		}
		const _NLOHMANN_JSON json& service { json["service"] };
		if (!service.contains("key") || !service["key"].is_object())
		{
			return Result<ServiceSummary>::failure(makeFailure(CatalogError::PROTOCOL_ERROR, "service.key must be an object"));
		}
		const _NLOHMANN_JSON json& key { service["key"] };
		Result<_STD string>		   ns { optionalString(key, "namespace") };
		if (!ns.isOk())
		{
			return Result<ServiceSummary>::failure(ns.error());
		}
		Result<_STD string> group { optionalString(key, "group") };
		if (!group.isOk())
		{
			return Result<ServiceSummary>::failure(group.error());
		}
		Result<_STD string> service_id { requiredString(key, "serviceId") };
		if (!service_id.isOk())
		{
			return Result<ServiceSummary>::failure(service_id.error());
		}
		Result<_STD string> service_name { optionalString(service, "serviceName") };
		if (!service_name.isOk())
		{
			return Result<ServiceSummary>::failure(service_name.error());
		}
		Result<_STD string> source { optionalString(service, "source") };
		if (!source.isOk())
		{
			return Result<ServiceSummary>::failure(source.error());
		}
		ServiceSummary summary {};
		summary.service.key.namespace_name = ns.value();
		summary.service.key.group_name	   = group.value();
		summary.service.key.service_id	   = service_id.value();
		summary.service.service_name	   = service_name.value();
		summary.service.source			   = source.value();
		summary.total_instances			   = json.value("totalInstances", 0ll);
		summary.healthy_instances		   = json.value("healthyInstances", 0ll);
		summary.runtime_status			   = json.value("runtimeStatus", _STD string {});
		return Result<ServiceSummary>::success(_STD move(summary));
	}

	_NODISCARD Result<CatalogServerInfo> ServiceGateway::parseCatalogServerInfo(const _NLOHMANN_JSON json& json)
	{
		if (!json.is_object())
		{
			return Result<CatalogServerInfo>::failure(makeFailure(CatalogError::PROTOCOL_ERROR, "catalog server info must be an object"));
		}
		Result<_STD string> ip { requiredString(json, "ip") };
		if (!ip.isOk())
		{
			return Result<CatalogServerInfo>::failure(ip.error());
		}
		Result<_STD string> node_id { requiredString(json, "nodeId") };
		if (!node_id.isOk())
		{
			return Result<CatalogServerInfo>::failure(node_id.error());
		}
		Result<_STD string> node_name { requiredString(json, "nodeName") };
		if (!node_name.isOk())
		{
			return Result<CatalogServerInfo>::failure(node_name.error());
		}
		if (!json.contains("multicastIp") || json["multicastIp"].is_null())
		{
			return Result<CatalogServerInfo>::failure(makeFailure(CatalogError::PROTOCOL_ERROR, "missing field: multicastIp"));
		}
		const _NLOHMANN_JSON json& multicast_node { json["multicastIp"] };
		if (!multicast_node.is_string() && !multicast_node.is_number())
		{
			return Result<CatalogServerInfo>::failure(makeFailure(CatalogError::PROTOCOL_ERROR, "invalid type: multicastIp"));
		}
		Result<_STD string> multicast_address { requiredString(json, "multicastAddress") };
		if (!multicast_address.isOk())
		{
			return Result<CatalogServerInfo>::failure(multicast_address.error());
		}
		CatalogServerInfo info {};
		info.ip			  = ip.value();
		info.node_id	  = node_id.value();
		info.node_name	  = node_name.value();
		info.multicast_ip = multicast_node.is_string() ? multicast_node.get<_STD string>() : _STD to_string(multicast_node.get<long long>());
		info.multicast_address = multicast_address.value();
		return Result<CatalogServerInfo>::success(_STD move(info));
	}

	_NODISCARD Result<_STD string> ServiceGateway::requiredString(const _NLOHMANN_JSON json& json, const _STD string& field)
	{
		if (!json.contains(field) || json[field].is_null() || !json[field].is_string())
		{
			return Result<_STD string>::failure(makeFailure(CatalogError::PROTOCOL_ERROR, "missing or invalid field: " + field));
		}
		return Result<_STD string>::success(json[field].get<_STD string>());
	}

	_NODISCARD Result<_STD string> ServiceGateway::optionalString(const _NLOHMANN_JSON json& json, const _STD string& field)
	{
		if (!json.contains(field) || json[field].is_null())
		{
			return Result<_STD string>::success("");
		}
		if (!json[field].is_string())
		{
			return Result<_STD string>::failure(makeFailure(CatalogError::PROTOCOL_ERROR, "invalid type: " + field));
		}
		return Result<_STD string>::success(json[field].get<_STD string>());
	}

	_NODISCARD Result<void> ServiceGateway::validateDataIds(const _STD vector<_STD string>& ids)
	{
		if (ids.empty())
		{
			return Result<void>::failure(makeFailure(CatalogError::INVALID_ARGUMENT, "data_ids is empty"));
		}
		_STD set<_STD string> distinct {};
		for (const auto& id : ids)
		{
			if (id.empty())
			{
				return Result<void>::failure(makeFailure(CatalogError::INVALID_ARGUMENT, "data_ids contains an empty value"));
			}
			if (!distinct.insert(id).second)
			{
				return Result<void>::failure(makeFailure(CatalogError::INVALID_ARGUMENT, "data_ids contains a duplicate value: " + id));
			}
		}
		return Result<void>::success();
	}

	_NODISCARD Result<void> ServiceGateway::validateConfigUploadRequest(const ConfigUploadRequest& request)
	{
		if (request.key.data_id.empty())
		{
			return Result<void>::failure(makeFailure(CatalogError::INVALID_ARGUMENT, "data_id is empty"));
		}
		if (request.content.empty())
		{
			return Result<void>::failure(makeFailure(CatalogError::INVALID_ARGUMENT, "content is empty"));
		}
		if (request.format.empty())
		{
			return Result<void>::failure(makeFailure(CatalogError::INVALID_ARGUMENT, "format is empty"));
		}
		for (const char ch : request.format)
		{
			const unsigned char uch { static_cast<unsigned char>(ch) };
			if (uch <= 0X20 || uch == 0X7f)
			{
				return Result<void>::failure(makeFailure(CatalogError::INVALID_ARGUMENT, "format contains whitespace or control characters"));
			}
		}
		return Result<void>::success();
	}

	_NODISCARD CatalogFailure ServiceGateway::remapNotFound(CatalogFailure failure, CatalogError replacement)
	{
		if (failure.http_status == 404)
		{
			return failureWithRetryable(failureWithCode(failure, replacement), false);
		}
		return failure;
	}
} // namespace plane::catalog::internal

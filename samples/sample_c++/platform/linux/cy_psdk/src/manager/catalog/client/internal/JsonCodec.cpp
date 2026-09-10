// cy_psdk/manager/catalog/client/internal/JsonCodec.cpp

#include "manager/catalog/client/internal/JsonCodec.h"

#include <set>

#include "manager/catalog/client/CatalogError.h"
#include "manager/catalog/client/CatalogFailure.h"
#include "manager/catalog/client/internal/TextUtil.h"

#include "define.h"

namespace plane::catalog::internal
{
	namespace JsonCodec
	{
		namespace
		{
			_NODISCARD const _STD set<_STD string>& validStatuses(void)
			{
				static const _STD set<_STD string> kValid { "UP", "DEGRADED", "DOWN", "UNKNOWN" };
				return kValid;
			}

			// 码点数近似按字节数 (ASCII 协议字段), 与服务端一致按 UTF-8 码点判断 <=128
			_NODISCARD bool exceeds128CodePoints(const _STD string& value)
			{
				_STD size_t code_points { 0 };
				for (const unsigned char ch : value)
				{
					if ((ch & 0Xc0) != 0X80)
					{
						++code_points;
					}
				}
				return code_points > 128;
			}
		} // namespace

		Result<_STD string> normalizeVersion(const _STD string& raw)
		{
			// 对齐 java String.trim(): 仅去除首尾空白 (<= 0x20), 内部字符原样保留
			_STD string value { trimAsciiWhitespaceCopy(raw) };
			if (value.empty())
			{
				return Result<_STD string>::failure(makeFailure(CatalogError::INVALID_ARGUMENT, "version is empty"));
			}
			if (exceeds128CodePoints(value))
			{
				return Result<_STD string>::failure(makeFailure(CatalogError::INVALID_ARGUMENT, "version exceeds 128 characters"));
			}
			return Result<_STD string>::success(_STD move(value));
		}

		Result<_NLOHMANN_JSON json> registrationToJson(const ServiceRegistration& registration, const _STD string& instance_address)
		{
			if (registration.service_id.empty())
			{
				return Result<_NLOHMANN_JSON json>::failure(makeFailure(CatalogError::INVALID_ARGUMENT, "service_id is empty"));
			}
			if (registration.service_name.empty())
			{
				return Result<_NLOHMANN_JSON json>::failure(makeFailure(CatalogError::INVALID_ARGUMENT, "service_name is empty"));
			}
			// 注: 允许空 exposed_ports (机载端当前无对外业务端口); 服务端以 HTTP 来源 IP 绑定实例。
			Result<_STD string> version { normalizeVersion(registration.version) };
			if (!version.isOk())
			{
				return Result<_NLOHMANN_JSON json>::failure(version.error());
			}

			_NLOHMANN_JSON json root;
			root["cluster"]				  = "DEFAULT";
			root["weight"]				  = 1.0;
			root["enabled"]				  = true;
			root["ephemeral"]			  = true;
			root["serviceName"]			  = registration.service_name;
			root["version"]				  = version.value();

			_NLOHMANN_JSON json endpoints = _NLOHMANN_JSON json::array();
			for (const auto& port : registration.exposed_ports)
			{
				_NLOHMANN_JSON json endpoint;
				endpoint["name"]	 = port.name;
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
				endpoint["metadata"] = _NLOHMANN_JSON json::object();
				endpoints.push_back(_STD move(endpoint));
			}
			root["endpoints"] = _STD move(endpoints);

			if (registration.metadata_json.empty())
			{
				root["metadata"] = _NLOHMANN_JSON json::object();
			}
			else
			{
				try
				{
					_NLOHMANN_JSON json metadata = _NLOHMANN_JSON json::parse(registration.metadata_json);
					if (!metadata.is_object())
					{
						return Result<_NLOHMANN_JSON json>::failure(makeFailure(CatalogError::INVALID_ARGUMENT, "metadata must be an object"));
					}
					for (auto it { metadata.begin() }; it != metadata.end(); ++it)
					{
						if (!it.value().is_string())
						{
							return Result<_NLOHMANN_JSON json>::
								failure(makeFailure(CatalogError::INVALID_ARGUMENT, "metadata values must be strings"));
						}
					}
					root["metadata"] = _STD move(metadata);
				}
				catch (const _STD exception& ex)
				{
					return Result<_NLOHMANN_JSON json>::failure(makeFailure(CatalogError::INVALID_ARGUMENT, ex.what()));
				}
			}
			return Result<_NLOHMANN_JSON json>::success(_STD move(root));
		}

		Result<_NLOHMANN_JSON json> statusToJson(const ServiceStatus& status)
		{
			const _STD string overall { status.overall_status.empty() ? (status.healthy ? "UP" : "DOWN") : status.overall_status };
			if (validStatuses().find(overall) == validStatuses().end())
			{
				return Result<_NLOHMANN_JSON json>::failure(makeFailure(CatalogError::PROTOCOL_ERROR, "invalid overall status"));
			}

			_NLOHMANN_JSON json root;
			root["overallStatus"] = overall;
			root["message"]		  = status.message;

			auto addComponent	  = [](_NLOHMANN_JSON json& components,
									   const _STD string&	name,
									   const _STD string&	component_status,
									   const _STD string&	code,
									   const _STD string&	message,
									   const _STD map<_STD string, _STD string>& details)
			{
				_NLOHMANN_JSON json component;
				component["status"]				 = component_status;
				component["code"]				 = code;
				component["message"]			 = message;
				_NLOHMANN_JSON json details_node = _NLOHMANN_JSON json::object();
				for (const auto& [key, value] : details)
				{
					details_node[key] = value;
				}
				component["details"] = _STD move(details_node);
				components[name]	 = _STD		move(component);
			};

			_NLOHMANN_JSON json components = _NLOHMANN_JSON json::object();
			if (status.components.empty())
			{
				addComponent(components, "application", status.healthy ? "UP" : "DOWN", status.code, status.message, status.details);
			}
			else
			{
				_STD set<_STD string> names {};
				for (const auto& component : status.components)
				{
					if (component.name.empty())
					{
						return Result<_NLOHMANN_JSON json>::failure(makeFailure(CatalogError::PROTOCOL_ERROR, "component name is empty"));
					}
					if (validStatuses().find(component.status) == validStatuses().end())
					{
						return Result<_NLOHMANN_JSON json>::failure(makeFailure(CatalogError::PROTOCOL_ERROR, "invalid component status"));
					}
					if (!names.insert(component.name).second)
					{
						return Result<_NLOHMANN_JSON json>::failure(makeFailure(CatalogError::PROTOCOL_ERROR, "duplicate component name"));
					}
					addComponent(components, component.name, component.status, component.code, component.message, component.details);
				}
			}
			root["components"] = _STD move(components);
			return Result<_NLOHMANN_JSON json>::success(_STD move(root));
		}
	} // namespace JsonCodec
} // namespace plane::catalog::internal

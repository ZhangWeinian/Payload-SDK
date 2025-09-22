#include "utils/JsonConverter/BuildAndParse.h"

#include "config/ConfigManager.h"
#include "services/mqtt/Handler/MessageHandler.h"
#include "utils/Logger/Logger.h"

#include "BuildAndParse.h"
#include <chrono>
#include <iomanip>
#include <sstream>

namespace plane::utils
{
	using n_json = ::nlohmann::json;

	static int64_t getCurrentTimestampMs(void) noexcept
	{
		using namespace _STD chrono;
		return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
	}

	static _STD string formatTimestamp(int64_t ms) noexcept
	{
		auto			  time_t { static_cast<_STD time_t>(ms / 1000) };
		_STD tm			  tm { *_STD localtime(&time_t) };
		_STD stringstream ss {};
		ss << _STD		  put_time(&tm, "%Y-%m-%d_%H:%M:%S");
		return ss.str();
	}

	_STD string JsonConverter::buildStatusReportJson(const protocol::StatusPayload& payload) noexcept
	{
		auto											  now { getCurrentTimestampMs() };
		_STD string										  plane_code { plane::config::ConfigManager::getInstance().getPlaneCode() };
		protocol::NetworkMessage<protocol::StatusPayload> msg { .ZBID = plane_code,
																.XXID = "SBZT-" + plane_code + "-" + _STD to_string(now),
																.XXLX = "SBZT",
																.SJC  = now,
																.SBSJ = formatTimestamp(now),
																.XXXX = payload };
		n_json											  jValue {};
		jValue["ZBID"] = msg.ZBID;
		jValue["XXID"] = msg.XXID;
		jValue["XXLX"] = msg.XXLX;
		jValue["SJC"]  = msg.SJC;
		jValue["SBSJ"] = msg.SBSJ;
		jValue["XXXX"] = msg.XXXX;
		return jValue.dump();
	}

	_STD string JsonConverter::buildMissionInfoJson(const protocol::MissionInfoPayload& payload) noexcept
	{
		auto												   now { getCurrentTimestampMs() };
		_STD string											   plane_code { plane::config::ConfigManager::getInstance().getPlaneCode() };
		protocol::NetworkMessage<protocol::MissionInfoPayload> msg { .ZBID = plane_code,
																	 .XXID = "GDXX-" + plane_code + "-" + _STD to_string(now),
																	 .XXLX = "GDXX",
																	 .SJC  = now,
																	 .SBSJ = formatTimestamp(now),
																	 .XXXX = payload };
		n_json												   jValue {};
		jValue["ZBID"] = msg.ZBID;
		jValue["XXID"] = msg.XXID;
		jValue["XXLX"] = msg.XXLX;
		jValue["SJC"]  = msg.SJC;
		jValue["SBSJ"] = msg.SBSJ;
		jValue["XXXX"] = msg.XXXX;
		return jValue.dump();
	}

	_STD string JsonConverter::buildHealthStatusJson(const _STD vector<protocol::HealthAlertPayload>& alerts) noexcept
	{
		auto						  now { getCurrentTimestampMs() };
		_STD string					  plane_code { plane::config::ConfigManager::getInstance().getMqttClientId() };
		protocol::HealthStatusPayload payload {};
		payload.GJLB = alerts;
		protocol::NetworkMessage<protocol::HealthStatusPayload> msg { .ZBID = plane_code,
																	  .XXID = "JKGL-" + plane_code + "-" + _STD to_string(now),
																	  .XXLX = "JKGL",
																	  .SJC	= now,
																	  .SBSJ = formatTimestamp(now),
																	  .XXXX = payload };
		n_json													jValue {};
		jValue["ZBID"] = msg.ZBID;
		jValue["XXID"] = msg.XXID;
		jValue["XXLX"] = msg.XXLX;
		jValue["SJC"]  = msg.SJC;
		jValue["SBSJ"] = msg.SBSJ;
		jValue["XXXX"] = msg.XXXX;
		return jValue.dump();
	}

	void JsonConverter::parseAndRouteMessage(_STD string_view topic, _STD string_view jsonString) noexcept
	{
		try
		{
			n_json j = n_json::parse(jsonString);
			if (j.contains("ZBID"))
			{
				_STD string targetPlaneCode { j.at("ZBID").get<_STD string>() };
				_STD string localPlaneCode { config::ConfigManager::getInstance().getPlaneCode() };
				if (targetPlaneCode != localPlaneCode)
				{
					LOG_DEBUG("收到发往其他设备 ({}) 的消息, 本机 ({}) 已忽略。", targetPlaneCode, localPlaneCode);
					return;
				}
			}
			else
			{
				LOG_WARN("收到的消息缺少 ZBID 字段, 无法验证目标设备。");
				return;
			}

			_STD string messageType { j.at("XXLX").get<_STD string>() };
			n_json		payloadJson = j.value("XXXX", n_json {});
			plane::services::MqttMessageHandler::getInstance().routeMessage(topic, messageType, payloadJson);
		}
		catch (const n_json::exception& e)
		{
			LOG_ERROR("处理 MQTT 消息时发生 JSON 错误 (主题: '{}'): {}. Raw JSON string:\n{}", topic, e.what(), jsonString);
		}
		catch (const _STD exception& e)
		{
			LOG_ERROR("处理 MQTT 消息时发生未知异常 (主题: '{}'): {}. Raw JSON string:\n{}", topic, e.what(), jsonString);
		}
	}
} // namespace plane::utils

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
		using namespace std::chrono;
		return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
	}

	static std::string formatTimestamp(int64_t ms) noexcept
	{
		auto			  time_t { static_cast<std::time_t>(ms / 1000) };
		std::tm			  tm { *std::localtime(&time_t) };
		std::stringstream ss {};
		ss << std::put_time(&tm, "%Y-%m-%d_%H:%M:%S");
		return ss.str();
	}

	std::string JsonConverter::buildStatusReportJson(const protocol::StatusPayload& payload) noexcept
	{
		auto											  now { getCurrentTimestampMs() };
		std::string										  plane_code { plane::config::ConfigManager::getInstance().getPlaneCode() };
		protocol::NetworkMessage<protocol::StatusPayload> msg { .ZBID = plane_code,
																.XXID = "SBZT-" + plane_code + "-" + std::to_string(now),
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

	std::string JsonConverter::buildMissionInfoJson(const protocol::MissionInfoPayload& payload) noexcept
	{
		auto												   now { getCurrentTimestampMs() };
		std::string											   plane_code { plane::config::ConfigManager::getInstance().getPlaneCode() };
		protocol::NetworkMessage<protocol::MissionInfoPayload> msg { .ZBID = plane_code,
																	 .XXID = "GDXX-" + plane_code + "-" + std::to_string(now),
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

	std::string JsonConverter::buildHealthStatusJson(const std::vector<protocol::HealthAlertPayload>& alerts) noexcept
	{
		auto						  now { getCurrentTimestampMs() };
		std::string					  plane_code { plane::config::ConfigManager::getInstance().getMqttClientId() };
		protocol::HealthStatusPayload payload {};
		payload.GJLB = alerts;
		protocol::NetworkMessage<protocol::HealthStatusPayload> msg { .ZBID = plane_code,
																	  .XXID = "JKGL-" + plane_code + "-" + std::to_string(now),
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

	void JsonConverter::parseAndRouteMessage(std::string_view topic, std::string_view jsonString) noexcept
	{
		try
		{
			n_json j = n_json::parse(jsonString);
			if (j.contains("ZBID"))
			{
				std::string targetPlaneCode { j.at("ZBID").get<std::string>() };
				std::string localPlaneCode { config::ConfigManager::getInstance().getPlaneCode() };
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

			std::string messageType { j.at("XXLX").get<std::string>() };
			n_json		payloadJson = j.value("XXXX", n_json {});
			plane::services::MqttMessageHandler::getInstance().routeMessage(topic, messageType, payloadJson);
		}
		catch (const n_json::exception& e)
		{
			LOG_ERROR("处理 MQTT 消息时发生 JSON 错误 (主题: '{}'): {}. Raw JSON string:\n{}", topic, e.what(), jsonString);
		}
		catch (const std::exception& e)
		{
			LOG_ERROR("处理 MQTT 消息时发生未知异常 (主题: '{}'): {}. Raw JSON string:\n{}", topic, e.what(), jsonString);
		}
	}
} // namespace plane::utils

#include "config/ConfigManager.h"
#include "protocol/JsonProtocol.h"
#include "utils/JsonConverter/JsonConverter.h"

#include <dji_logger.h>

#include <chrono>
#include <iomanip>
#include <sstream>

namespace plane::utils
{
	using n_json = ::nlohmann::json;

	static int64_t getCurrentTimestampMs()
	{
		using namespace std::chrono;
		return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
	}

	static std::string formatTimestamp(int64_t ms)
	{
		auto			  time_t { static_cast<std::time_t>(ms / 1000) };
		std::tm			  tm { *std::localtime(&time_t) };
		std::stringstream ss {};
		ss << std::put_time(&tm, "%Y-%m-%d_%H:%M:%S");
		return ss.str();
	}

	std::string JsonConverter::buildStatusReportJson(const protocol::StatusPayload& payload)
	{
		auto											  now { getCurrentTimestampMs() };
		std::string										  plane_code { plane::config::ConfigManager::getInstance().getMqttClientId() };
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

	std::string JsonConverter::buildMissionInfoJson(const protocol::MissionInfoPayload& payload)
	{
		auto												   now { getCurrentTimestampMs() };
		std::string											   plane_code { plane::config::ConfigManager::getInstance().getMqttClientId() };
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

	std::string JsonConverter::buildHealthStatusJson(const std::vector<protocol::HealthAlertPayload>& alerts)
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
} // namespace plane::utils

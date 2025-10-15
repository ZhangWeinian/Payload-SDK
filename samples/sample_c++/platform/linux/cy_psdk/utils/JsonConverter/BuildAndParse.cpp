// cy_psdk/utils/JsonConverter/BuildAndParse.cpp

#include "BuildAndParse.h"

#include "config/ConfigManager.h"
#include "services/MQTT/Handler/MessageHandler.h"
#include "utils/Logger.h"

#include <chrono>
#include <iomanip>
#include <sstream>

namespace plane::utils
{
	using n_json = _NLOHMANN_JSON json;

	namespace
	{
		static int64_t getCurrentTimestampMs(void) noexcept
		{
			return _STD_CHRONO duration_cast<_STD_CHRONO milliseconds>(_STD_CHRONO system_clock::now().time_since_epoch()).count();
		}

		static _STD string formatTimestamp(int64_t ms) noexcept
		{
			auto			  time_t { static_cast<_STD time_t>(ms / 1000) };
			_STD tm			  tm_buf {};
			_CSTD			  localtime_r(&time_t, &tm_buf);
			_STD stringstream ss {};
			ss << _STD		  put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
			return ss.str();
		}
	} // namespace

	_STD string JsonConverter::buildStatusReportJson(const plane::protocol::StatusPayload& payload) noexcept
	{
		static const _STD string plane_code { plane::config::ConfigManager::getInstance().getPlaneCode() };
		auto					 now { _UNNAMED getCurrentTimestampMs() };
		plane::protocol::NetworkMessage<plane::protocol::StatusPayload> msg { .ZBID = plane_code,
																			  .XXID = "SBZT-" + plane_code + "-" + _STD to_string(now),
																			  .XXLX = "SBZT",
																			  .SJC	= now,
																			  .SBSJ = _UNNAMED formatTimestamp(now),
																			  .XXXX = payload };
		n_json															j = msg;
		return j.dump();
	}

	_STD string JsonConverter::buildMissionInfoJson(const plane::protocol::MissionInfoPayload& payload) noexcept
	{
		static const _STD string plane_code { plane::config::ConfigManager::getInstance().getPlaneCode() };
		auto					 now { _UNNAMED getCurrentTimestampMs() };
		plane::protocol::NetworkMessage<plane::protocol::MissionInfoPayload> msg { .ZBID = plane_code,
																				   .XXID = "GDXX-" + plane_code + "-" + _STD to_string(now),
																				   .XXLX = "GDXX",
																				   .SJC	 = now,
																				   .SBSJ = _UNNAMED formatTimestamp(now),
																				   .XXXX = payload };
		n_json																 j = msg;
		return j.dump();
	}

	_STD string JsonConverter::buildHealthStatusJson(const plane::protocol::HealthStatusPayload& payload) noexcept
	{
		static const _STD string plane_code { plane::config::ConfigManager::getInstance().getPlaneCode() };
		auto					 now { _UNNAMED getCurrentTimestampMs() };
		plane::protocol::NetworkMessage<plane::protocol::HealthStatusPayload> msg { .ZBID = plane_code,
																					.XXID = "JKGL-" + plane_code + "-" + _STD to_string(now),
																					.XXLX = "JKGL",
																					.SJC  = now,
																					.SBSJ = _UNNAMED formatTimestamp(now),
																					.XXXX = payload };
		n_json																  j = msg;
		return j.dump();
	}

	_STD string JsonConverter::buildMissionProgressJson(const plane::protocol::MissionProgressPayload& payload) noexcept
	{
		static const _STD string plane_code { plane::config::ConfigManager::getInstance().getPlaneCode() };
		auto					 now { _UNNAMED getCurrentTimestampMs() };
		plane::protocol::NetworkMessage<plane::protocol::MissionProgressPayload> msg { .ZBID = plane_code,
																					   .XXID = "RWJD-" + plane_code + "-" + _STD to_string(now),
																					   .XXLX = "RWJD",
																					   .SJC	 = now,
																					   .SBSJ = _UNNAMED formatTimestamp(now),
																					   .XXXX = payload };
		n_json																	 j = msg;
		return j.dump();
	}

	void JsonConverter::parseAndRouteMessage(_STD string_view topic, _STD string_view jsonString) noexcept
	{
		try
		{
			n_json j = n_json::parse(jsonString);
			if (j.contains("ZBID"))
			{
				_STD string target_plane_code { j.at("ZBID").get<_STD string>() };
				_STD string local_plane_code { plane::config::ConfigManager::getInstance().getPlaneCode() };
				if (target_plane_code != local_plane_code)
				{
					LOG_DEBUG("收到发往其他设备 ({}) 的消息, 本机 ({}) 已忽略。", target_plane_code, local_plane_code);
					return;
				}
			}
			else
			{
				LOG_WARN("收到的消息缺少 ZBID 字段, 无法验证目标设备。");
				return;
			}

			_STD string message_type { j.at("XXLX").get<_STD string>() };
			n_json		payload_json = j.value("XXXX", n_json {});
			plane::services::MqttMessageHandler::getInstance().routeMessage(topic, message_type, payload_json);
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

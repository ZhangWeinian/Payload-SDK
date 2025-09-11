#include "config/ConfigManager.h"
#include "protocol/JsonProtocol.h"
#include "utils/JsonConverter.h"

#include <dji_logger.h>

#include <chrono>
#include <iomanip>
#include <sstream>

namespace plane::utils
{
	using n_json = nlohmann::json;

	static int64_t getCurrentTimestampMs()
	{
		using namespace std::chrono;
		return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
	}

	static std::string formatTimestamp(int64_t ms)
	{
		auto			  time_t = static_cast<std::time_t>(ms / 1000);
		std::tm			  tm	 = *std::localtime(&time_t);
		std::stringstream ss;
		ss << std::put_time(&tm, "%Y-%m-%d_%H:%M:%S");
		return ss.str();
	}

	std::string JsonConverter::buildStatusReportJson(const protocol::StatusPayload& payload)
	{
		auto											  now		 = getCurrentTimestampMs();
		std::string										  plane_code = ConfigManager::getInstance().getMqttClientId();
		protocol::NetworkMessage<protocol::StatusPayload> msg		 = { .planeCode			 = plane_code,
																		 .messageId			 = "SBZT-" + plane_code + "-" + std::to_string(now),
																		 .messageType		 = "SBZT",
																		 .timestamp			 = now,
																		 .formattedTimestamp = formatTimestamp(now),
																		 .payload			 = payload };
		n_json											  jValue	 = {};
		jValue["ZBID"]												 = msg.planeCode;
		jValue["XXID"]												 = msg.messageId;
		jValue["XXLX"]												 = msg.messageType;
		jValue["SJC"]												 = msg.timestamp;
		jValue["SBSJ"]												 = msg.formattedTimestamp;
		jValue["XXXX"]												 = msg.payload;
		return jValue.dump();
	}

	std::string JsonConverter::buildMissionInfoJson(const protocol::MissionInfoPayload& payload)
	{
		auto												   now		  = getCurrentTimestampMs();
		std::string											   plane_code = ConfigManager::getInstance().getMqttClientId();
		protocol::NetworkMessage<protocol::MissionInfoPayload> msg		  = { .planeCode		  = plane_code,
																			  .messageId		  = "GDXX-" + plane_code + "-" + std::to_string(now),
																			  .messageType		  = "GDXX",
																			  .timestamp		  = now,
																			  .formattedTimestamp = formatTimestamp(now),
																			  .payload			  = payload };
		n_json												   jValue	  = {};
		jValue["ZBID"]													  = msg.planeCode;
		jValue["XXID"]													  = msg.messageId;
		jValue["XXLX"]													  = msg.messageType;
		jValue["SJC"]													  = msg.timestamp;
		jValue["SBSJ"]													  = msg.formattedTimestamp;
		jValue["XXXX"]													  = msg.payload;
		return jValue.dump();
	}

	std::string JsonConverter::buildHealthStatusJson(const std::vector<protocol::HealthAlertPayload>& alerts)
	{
		auto						  now		 = getCurrentTimestampMs();
		std::string					  plane_code = ConfigManager::getInstance().getMqttClientId();
		protocol::HealthStatusPayload payload;
		payload.alerts												   = alerts;
		protocol::NetworkMessage<protocol::HealthStatusPayload> msg	   = { .planeCode		   = plane_code,
																		   .messageId		   = "JKGL-" + plane_code + "-" + std::to_string(now),
																		   .messageType		   = "JKGL",
																		   .timestamp		   = now,
																		   .formattedTimestamp = formatTimestamp(now),
																		   .payload			   = payload };
		n_json													jValue = {};
		jValue["ZBID"]												   = msg.planeCode;
		jValue["XXID"]												   = msg.messageId;
		jValue["XXLX"]												   = msg.messageType;
		jValue["SJC"]												   = msg.timestamp;
		jValue["SBSJ"]												   = msg.formattedTimestamp;
		jValue["XXXX"]												   = msg.payload;
		return jValue.dump();
	}

	void JsonConverter::parseAndRouteMessage(const std::string& topic, const std::string& jsonString)
	{
		try
		{
			n_json		jValue		= n_json::parse(jsonString);
			std::string messageType = jValue.at("XXLX").get<std::string>();
			n_json		payloadJson = jValue.at("XXXX");

			USER_LOG_INFO("Routing message with type '%s' from topic '%s'", messageType.c_str(), topic.c_str());

			if (messageType == "WAYPOINT_CMD")
			{
				auto payload = payloadJson.get<protocol::WaypointPayload>();
				USER_LOG_INFO("Waypoint command received with %zu waypoints.", payload.waypoints.size());
				// TODO: MissionManager::getInstance().handleWaypointCommand(payload);
			}
			else if (messageType == "TAKEOFF_CMD")
			{
				auto payload = payloadJson.get<protocol::TakeoffPayload>();
				USER_LOG_INFO("Takeoff command received, target altitude: %f", payload.targetAltitude);
				// TODO: FlightController::getInstance().handleTakeoffCommand(payload);
			}
			else if (messageType == "CIRCLE_FLY_CMD")
			{
				auto payload = payloadJson.get<protocol::CircleFlyPayload>();
				USER_LOG_INFO("Circle fly command received at lat/lon (%f, %f) with radius %f.",
							  payload.latitude,
							  payload.longitude,
							  payload.radius);
				// TODO: MissionManager::getInstance().handleCircleFlyCommand(payload);
			}
			else if (messageType == "GIMBAL_CTRL")
			{
				auto payload = payloadJson.get<protocol::GimbalControlPayload>();
				USER_LOG_INFO("Gimbal control received: pitch=%f, yaw=%f, mode=%d", payload.pitch, payload.yaw, payload.mode);
				// TODO: GimbalController::getInstance().handleControlCommand(payload);
			}
			else if (messageType == "ZOOM_CTRL")
			{
				auto payload = payloadJson.get<protocol::ZoomControlPayload>();
				USER_LOG_INFO("Zoom control received: factor=%f, source=%s",
							  payload.zoomFactor.value_or(0.0),
							  payload.cameraSource.value_or("N/A").c_str());
				// TODO: CameraController::getInstance().handleZoomCommand(payload);
			}
			else if (messageType == "VIRTUAL_STICK_DATA")
			{
				auto payload = payloadJson.get<protocol::StickDataPayload>();
				USER_LOG_INFO("Virtual stick data received: T:%d, Y:%d, P:%d, R:%d", payload.throttle, payload.yaw, payload.pitch, payload.roll);
				// TODO: VirtualStickManager::getInstance().handleStickData(payload);
			}
			else if (messageType == "VIRTUAL_STICK_SWITCH")
			{
				auto payload = payloadJson.get<protocol::StickModeSwitchPayload>();
				USER_LOG_INFO("Virtual stick mode switch received: mode=%d", payload.stickMode);
				// TODO: VirtualStickManager::getInstance().handleModeSwitch(payload);
			}
			else if (messageType == "NED_VELOCITY_CTRL")
			{
				auto payload = payloadJson.get<protocol::NedVelocityPayload>();
				USER_LOG_INFO("NED velocity control received: N:%.2f, E:%.2f, D:%.2f, YawRate:%.2f",
							  payload.velocityNorth,
							  payload.velocityEast,
							  payload.velocityDown,
							  payload.yawRate);
				// TODO: FlightController::getInstance().handleNedVelocityCommand(payload);
			}
			else if (messageType == "CONTROL_STRATEGY")
			{
				auto payload = payloadJson.get<protocol::ControlStrategyPayload>();
				USER_LOG_INFO("Control strategy switch received: strategy_code=%d", payload.strategyCode);
				// TODO: SystemManager::getInstance().handleControlStrategy(payload);
			}
			else
			{
				USER_LOG_WARN("Unknown or unhandled message type '%s' received on topic '%s'.", messageType.c_str(), topic.c_str());
			}
		}
		catch (const n_json::exception& e)
		{
			USER_LOG_ERROR("nlohmann/json error on topic '%s': %s", topic.c_str(), e.what());
		}
		catch (const std::exception& e)
		{
			USER_LOG_ERROR("An unexpected error occurred while processing message on topic '%s': %s", topic.c_str(), e.what());
		}
	}
} // namespace plane::utils

#include "services/mqtt/MqttMessageHandler.h"
#include "services/mqtt/MqttTopics.h"

#include <dji_logger.h>

namespace plane::services
{
	namespace MqttMessageHandler
	{
		void processMissionControl(const std::string& messageType, const n_json& payloadJson);
		void processCommandControl(const std::string& messageType, const n_json& payloadJson);
		void processPayloadControl(const std::string& messageType, const n_json& payloadJson);
		void processRockerControl(const std::string& messageType, const n_json& payloadJson);
		void processVelocityControl(const std::string& messageType, const n_json& payloadJson);

		void handleWaypointMission(const protocol::WaypointPayload& payload);

		void routeMessage(const std::string& topic, const std::string& messageType, const n_json& payloadJson)
		{
			if (topic == mqtt::TOPIC_MISSION_CONTROL)
			{
				processMissionControl(messageType, payloadJson);
			}
			else if (topic == mqtt::TOPIC_COMMAND_CONTROL)
			{
				processCommandControl(messageType, payloadJson);
			}
			else if (topic == mqtt::TOPIC_PAYLOAD_CONTROL)
			{
				processPayloadControl(messageType, payloadJson);
			}
			else if (topic == mqtt::TOPIC_ROCKER_CONTROL)
			{
				processRockerControl(messageType, payloadJson);
			}
			else if (topic == mqtt::TOPIC_VELOCITY_CONTROL)
			{
				processVelocityControl(messageType, payloadJson);
			}
			else
			{
				USER_LOG_ERROR("[MQTT] 收到未知主题的消息: %s", topic.c_str());
			}
		}

		void processMissionControl(const std::string& messageType, const n_json& payloadJson)
		{
			if (messageType == "XFHXRW")
			{
				auto payload = payloadJson.get<protocol::WaypointPayload>();
				handleWaypointMission(payload);
			}
			else
			{
				USER_LOG_ERROR("[MQTT] mission_control 未知操作类型: %s", messageType.c_str());
			}
		}

		void handleWaypointMission(const protocol::WaypointPayload& payload)
		{
			if (payload.waypoints.empty())
			{
				return;
			}

			if (payload.waypoints.size() == 1)
			{
				USER_LOG_INFO("[MQTT] 收到并准备执行【单航点任务】");
				// TODO: 调用 FlyManager 的 flyToPoint(payload.waypoints[0]);
			}
			else
			{
				USER_LOG_INFO("[MQTT] 收到并准备执行【航线任务】");
				// TODO: 判断是否需要航线优化 (SettingsManager.isWaypointOptimizationEnabled)
				// TODO: 如果需要，调用 FlyManager 的 optimizeWaypoints(payload.waypoints)
				// TODO: 调用 FlyManager 的 waypointFly(...);
			}
		}

		void processCommandControl(const std::string& messageType, const n_json& payloadJson)
		{
			if (messageType == "QF")
			{
				auto payload = payloadJson.get<protocol::TakeoffPayload>();
				USER_LOG_INFO("[MQTT] 收到【起飞】指令, 目标高度: %f", payload.targetAltitude);
				// TODO: 调用 FlyManager 的 takeoff(payload.targetAltitude);
			}
			else if (messageType == "JL")
			{
				USER_LOG_INFO("[MQTT] 收到【返航】指令");
				// TODO: 调用 FlyManager 的 goHome();
			}
			else if (messageType == "XT")
			{
				USER_LOG_INFO("[MQTT] 收到【悬停】指令");
				// TODO: 调用 FlyManager 的 hover();
			}
			else if (messageType == "YTJSCL")
			{
				auto payload = payloadJson.get<protocol::ControlStrategyPayload>();
				USER_LOG_INFO("[MQTT] 收到【云台控制策略切换】指令, 策略代码: %d", payload.strategyCode);
				// TODO: 根据 payload.strategyCode (0,1,2) 映射到 C++ 枚举或类型
				// TODO: 调用 FlyManager 的 setControlStrategy(...)，并处理成功/失败回调
			}
			else if (messageType == "ZNHR")
			{
				auto payload = payloadJson.get<protocol::CircleFlyPayload>();
				USER_LOG_INFO("[MQTT] 收到【智能环绕】指令");
				// TODO: 调用 FlyManager 的 flyCircleAroundPoint(...)
			}
			else
			{
				USER_LOG_ERROR("[MQTT] command_control 未知操作类型: %s", messageType.c_str());
			}
		}

		void processPayloadControl(const std::string& messageType, const n_json& payloadJson)
		{
			if (messageType == "YTKZ")
			{
				auto payload = payloadJson.get<protocol::GimbalControlPayload>();
				if (payload.mode == 0)
				{ // 角度控制
					USER_LOG_INFO("[MQTT] 收到【云台角度控制】指令: pitch=%f, yaw=%f", payload.pitch, payload.yaw);
					// TODO: 调用 FlyManager 的 rotateGimbal(payload.pitch, payload.yaw);
				}
				else
				{ // 速度控制
					USER_LOG_INFO("[MQTT] 收到【云台速度控制】指令: pitch=%f, yaw=%f", payload.pitch, payload.yaw);
					// TODO: 调用 FlyManager 的 rotateGimbalBySpeed(payload.pitch, payload.yaw, 0.0);
				}
			}
			else if (messageType == "BJKZ")
			{
				auto payload = payloadJson.get<protocol::ZoomControlPayload>();
				if (payload.zoomFactor)
				{
					USER_LOG_INFO("[MQTT] 收到【相机变焦】指令: factor=%f", *payload.zoomFactor);
					// TODO: 调用 FlyManager 的 setCameraZoomFactor(*payload.zoomFactor);
				}
				if (payload.cameraSource)
				{
					USER_LOG_INFO("[MQTT] 收到【相机视频源切换】指令: source=%s", (*payload.cameraSource).c_str());
					// TODO: 根据 *payload.cameraSource ("ir", "wide", "zoom") 映射到 PSDK 的相机源枚举
					// TODO: 调用 FlyManager 的 setCameraStreamSource(...);
				}
			}
			else
			{
				USER_LOG_ERROR("[MQTT] payload_control 未知操作类型: %s", messageType.c_str());
			}
		}

		void processRockerControl(const std::string& messageType, const n_json& payloadJson)
		{
			// TODO: 在这里检查虚拟摇杆功能是否开启 (SettingsManager.isVirtualStickFeatureEnabled)
			if (messageType == "YGFXZL")
			{
				auto payload = payloadJson.get<protocol::StickDataPayload>();
				// USER_LOG_DEBUG 可能会输出过多，酌情使用
				USER_LOG_INFO("[MQTT] 收到【虚拟摇杆数据】: T:%d Y:%d P:%d R:%d", payload.throttle, payload.yaw, payload.pitch, payload.roll);
				// TODO: 调用 FlyManager 的 sendRawStickData(...);
			}
			else if (messageType == "YGMSQH")
			{
				auto payload = payloadJson.get<protocol::StickModeSwitchPayload>();
				USER_LOG_INFO("[MQTT] 收到【虚拟摇杆模式切换】指令: mode=%d", payload.stickMode);
				// TODO: 根据 payload.stickMode (0,1,2) 调用 FlyManager 的 enable/disableVirtualStick，并处理回调
			}
			else
			{
				USER_LOG_ERROR("[MQTT] rocker_control 未知操作类型: %s", messageType.c_str());
			}
		}

		void processVelocityControl(const std::string& messageType, const n_json& payloadJson)
		{
			// TODO: 在这里检查虚拟摇杆功能是否开启
			if (messageType == "SDKZ")
			{
				auto payload = payloadJson.get<protocol::NedVelocityPayload>();
				USER_LOG_INFO("[MQTT] 收到【NED速度控制】指令: N:%.2f, E:%.2f, D:%.2f, YawRate:%.2f",
							  payload.velocityNorth,
							  payload.velocityEast,
							  payload.velocityDown,
							  payload.yawRate);
				// TODO: 调用 FlyManager 的 sendNedVelocityCommand(...);
			}
		}
	} // namespace MqttMessageHandler
} // namespace plane::services

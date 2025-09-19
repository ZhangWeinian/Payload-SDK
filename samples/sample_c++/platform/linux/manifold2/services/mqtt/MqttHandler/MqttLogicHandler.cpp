#include "services/mqtt/MqttHandler/MqttLogicHandler.h"

#include "protocol/JsonProtocol.h"
#include "services/drone_control/FlyManager/FlyManager.h"
#include "services/mqtt/MqttHandler/MqttMessageHandler.h"
#include "services/mqtt/MqttTopics.h"
#include "utils/JsonConverter/JsonToKmzConverter.h"
#include "utils/Logger/Logger.h"

namespace plane::services::MqttLogicHandler
{
	using n_json = ::nlohmann::json;

	void handleWaypointMission(const n_json& payloadJson)
	{
		try
		{
			auto payload { payloadJson.get<plane::protocol::WaypointPayload>() };
			if (payload.HDJ.empty())
			{
				LOG_WARN("[MQTT] 收到的航点任务 (RWID: {}) 中不包含任何航点。", payload.RWID.value_or("N/A"));
				return;
			}

			if (payload.HDJ.size() == 1)
			{
				LOG_INFO("[MQTT] 收到并准备执行【单航点任务】");
				plane::services::FlyManager::getInstance().flyToPoint(payload.HDJ[0]);
				return;
			}
			else
			{
				LOG_INFO("[MQTT] 收到并准备执行【航线任务】，共 {} 个航点", payload.HDJ.size());
				if (!plane::utils::JsonToKmzConverter::convertWaypointsToKmz(payload.HDJ, payload))
				{
					LOG_ERROR("生成 KMZ 文件失败！任务中止。");
					return;
				}
				plane::services::FlyManager::getInstance().waypointFly(plane::utils::JsonToKmzConverter::getKmzFilePath());
			}
		}
		catch (const n_json::exception& e)
		{
			LOG_ERROR("解析航点任务 (XFHXRW) 失败: {}", e.what());
		}
	}

	void handleTakeoff(const n_json& payloadJson)
	{
		LOG_INFO("[MQTT] 收到【起飞】指令:");
		const auto& payload { payloadJson.get<plane::protocol::TakeoffPayload>() };
		plane::services::FlyManager::getInstance().takeoff(payload);
	}

	void handleGoHome(const n_json& payloadJson)
	{
		LOG_INFO("[MQTT] 收到【返航】指令");
		plane::services::FlyManager::getInstance().goHome();
	}

	void handleHover(const n_json& payloadJson)
	{
		LOG_INFO("[MQTT] 收到【悬停】指令");
		plane::services::FlyManager::getInstance().hover();
	}

	void handleLand(const n_json& payloadJson)
	{
		LOG_INFO("[MQTT] 收到【降落】指令，准备执行...");
		plane::services::FlyManager::getInstance().land();
	}

	void handleControlStrategySwitch(const n_json& payloadJson)
	{
		auto payload { payloadJson.get<plane::protocol::ControlStrategyPayload>() };
		if (payload.YTJSCL)
		{
			LOG_INFO("[MQTT] 收到【云台控制策略切换】指令, 策略代码: {}", *payload.YTJSCL);
			// TODO: 根据 *payload.YTJSCL (0,1,2) 映射到 C++ 枚举或类型
			// TODO: 调用 FlyManager 的 setControlStrategy(...)，并处理成功/失败回调
		}
	}

	void handleCircleFly(const n_json& payloadJson)
	{
		auto payload { payloadJson.get<plane::protocol::CircleFlyPayload>() };
		LOG_INFO("[MQTT] 收到【智能环绕】指令: lat={}, lon={}, alt={}, r={}, spd={}",
				 payload.WD,
				 payload.JD,
				 payload.GD,
				 payload.BJ,
				 payload.SD);
		// TODO: 调用 FlyManager 的 flyCircleAroundPoint(...)
	}

	void handleGimbalControl(const n_json& payloadJson)
	{
		auto payload { payloadJson.get<plane::protocol::GimbalControlPayload>() };
		if (payload.MS == 0) // 角度控制
		{
			LOG_INFO("[MQTT] 收到【云台角度控制】指令: pitch={}, yaw={}", payload.FYJ, payload.PHJ);
			// TODO: 调用 FlyManager 的 rotateGimbal(payload.pitch, payload.yaw);
		}
		else // 速度控制
		{
			LOG_INFO("[MQTT] 收到【云台速度控制】指令: pitch={}, yaw={}", payload.FYJ, payload.PHJ);
			// TODO: 调用 FlyManager 的 rotateGimbalBySpeed(payload.pitch, payload.yaw, 0.0);
		}
	}

	void handleCameraControl(const n_json& payloadJson)
	{
		auto payload { payloadJson.get<plane::protocol::ZoomControlPayload>() };
		if (payload.BJB)
		{
			LOG_INFO("[MQTT] 收到【相机变焦】指令: factor={}", *payload.BJB);
			// TODO: 调用 FlyManager 的 setCameraZoomFactor(*payload.BJB);
		}
		if (payload.XJLX)
		{
			LOG_INFO("[MQTT] 收到【相机视频源切换】指令: source={}", *payload.XJLX);
			// TODO: 根据 *payload.XJLX ("ir", "wide", "zoom") 映射到 PSDK 的相机源枚举
			// TODO: 调用 FlyManager 的 setCameraStreamSource(...);
		}
	}

	void handleStickData(const n_json& payloadJson)
	{
		// TODO: 在这里检查虚拟摇杆功能是否开启 (SettingsManager.isVirtualStickFeatureEnabled)
		auto payload { payloadJson.get<plane::protocol::StickDataPayload>() };
		// TODO: 调用 FlyManager 的 sendRawStickData(...);
	}

	void handleStickModeSwitch(const n_json& payloadJson)
	{
		auto payload { payloadJson.get<plane::protocol::StickModeSwitchPayload>() };
		LOG_INFO("[MQTT] 收到【虚拟摇杆模式切换】指令: mode={}", payload.YGMS);
		// TODO: 根据 payload.stickMode (0,1,2) 调用 FlyManager 的 enable/disableVirtualStick，并处理回调
	}

	void handleNedVelocity(const n_json& payloadJson)
	{
		// TODO: 在这里检查虚拟摇杆/速度控制功能是否开启
		auto payload { payloadJson.get<plane::protocol::NedVelocityPayload>() };
		LOG_DEBUG("[MQTT] 收到【NED速度控制】指令: N:{:.2f}, E:{:.2f}, D:{:.2f}, YawRate:{:.2f}",
				  payload.SDN,
				  payload.SDD,
				  payload.SDX,
				  payload.PHJ);
		// TODO: 调用 FlyManager 的 sendNedVelocityCommand(...);
	}

	void initialize()
	{
		LOG_INFO("正在初始化 MQTT 业务逻辑处理器...");
		auto& msg_handler { MqttMessageHandler::getInstance() };
		msg_handler.registerHandler(TOPIC_MISSION_CONTROL, "XFHXRW", handleWaypointMission);
		msg_handler.registerHandler(TOPIC_COMMAND_CONTROL, "QF", handleTakeoff);
		msg_handler.registerHandler(TOPIC_COMMAND_CONTROL, "FH", handleGoHome);
		msg_handler.registerHandler(TOPIC_COMMAND_CONTROL, "XT", handleHover);
		msg_handler.registerHandler(TOPIC_COMMAND_CONTROL, "JL", handleLand);
		msg_handler.registerHandler(TOPIC_COMMAND_CONTROL, "YTJSCL", handleControlStrategySwitch);
		msg_handler.registerHandler(TOPIC_COMMAND_CONTROL, "ZNHR", handleCircleFly);
		msg_handler.registerHandler(TOPIC_PAYLOAD_CONTROL, "YTKZ", handleGimbalControl);
		msg_handler.registerHandler(TOPIC_PAYLOAD_CONTROL, "BJKZ", handleCameraControl);
		msg_handler.registerHandler(TOPIC_ROCKER_CONTROL, "YGFXZL", handleStickData);
		msg_handler.registerHandler(TOPIC_ROCKER_CONTROL, "YGMSQH", handleStickModeSwitch);
		msg_handler.registerHandler(TOPIC_VELOCITY_CONTROL, "SDKZ", handleNedVelocity);
		LOG_INFO("所有 MQTT 业务逻辑处理器注册完毕。");
	}
} // namespace plane::services::MqttLogicHandler

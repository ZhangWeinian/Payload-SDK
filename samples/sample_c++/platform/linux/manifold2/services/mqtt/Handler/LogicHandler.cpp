// manifold2/services/mqtt/Handler/LogicHandler.cpp

#include "services/mqtt/Handler/LogicHandler.h"

#include "protocol/DroneDataClass.h"
#include "services/DroneControl/FlyManager.h"
#include "services/mqtt/Handler/MessageHandler.h"
#include "services/mqtt/Topics.h"
#include "utils/JsonConverter/JsonToKmz.h"
#include "utils/Logger/Logger.h"

namespace plane::services
{
	using n_json = ::nlohmann::json;

	bool LogicHandler::init(void) noexcept
	{
		try
		{
			LOG_DEBUG("正在初始化 MQTT 业务逻辑处理器...");
			auto& msg_handler { MqttMessageHandler::getInstance() };
			msg_handler.registerHandler(plane::services::TOPIC_MISSION_CONTROL, "XFHXRW", &LogicHandler::handleWaypointMission);
			msg_handler.registerHandler(plane::services::TOPIC_COMMAND_CONTROL, "QF", &LogicHandler::handleTakeoff);
			msg_handler.registerHandler(plane::services::TOPIC_COMMAND_CONTROL, "FH", &LogicHandler::handleGoHome);
			msg_handler.registerHandler(plane::services::TOPIC_COMMAND_CONTROL, "XT", &LogicHandler::handleHover);
			msg_handler.registerHandler(plane::services::TOPIC_COMMAND_CONTROL, "JL", &LogicHandler::handleLand);
			msg_handler.registerHandler(plane::services::TOPIC_COMMAND_CONTROL, "YTJSCL", &LogicHandler::handleControlStrategySwitch);
			msg_handler.registerHandler(plane::services::TOPIC_COMMAND_CONTROL, "ZNHR", &LogicHandler::handleCircleFly);
			msg_handler.registerHandler(plane::services::TOPIC_PAYLOAD_CONTROL, "YTKZ", &LogicHandler::handleGimbalControl);
			msg_handler.registerHandler(plane::services::TOPIC_PAYLOAD_CONTROL, "BJKZ", &LogicHandler::handleCameraControl);
			msg_handler.registerHandler(plane::services::TOPIC_ROCKER_CONTROL, "YGFXZL", &LogicHandler::handleStickData);
			msg_handler.registerHandler(plane::services::TOPIC_ROCKER_CONTROL, "YGMSQH", &LogicHandler::handleStickModeSwitch);
			msg_handler.registerHandler(plane::services::TOPIC_VELOCITY_CONTROL, "SDKZ", &LogicHandler::handleNedVelocity);
		}
		catch (const _STD exception& e)
		{
			LOG_ERROR("初始化 MQTT 业务逻辑处理器时发生异常: {}", e.what());
			return false;
		}

		return true;
	}

	void LogicHandler::handleWaypointMission(const n_json& payloadJson) noexcept
	{
		try
		{
			auto payload = payloadJson.get<plane::protocol::WaypointPayload>();
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
				LOG_INFO("[MQTT] 收到并准备执行【航线任务】, 共 {} 个航点", payload.HDJ.size());
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
			LOG_ERROR("解析航点任务失败: {}, payloadJson:\n{}", e.what(), payloadJson.dump());
		}
	}

	void LogicHandler::handleTakeoff(const n_json& payloadJson) noexcept
	{
		LOG_INFO("[MQTT] 收到【起飞】指令:");
		const auto& payload { payloadJson.get<plane::protocol::TakeoffPayload>() };
		plane::services::FlyManager::getInstance().takeoff(payload);
	}

	void LogicHandler::handleGoHome(const n_json& payloadJson) noexcept
	{
		LOG_INFO("[MQTT] 收到【返航】指令");
		plane::services::FlyManager::getInstance().goHome();
	}

	void LogicHandler::handleHover(const n_json& payloadJson) noexcept
	{
		LOG_INFO("[MQTT] 收到【悬停】指令");
		plane::services::FlyManager::getInstance().hover();
	}

	void LogicHandler::handleLand(const n_json& payloadJson) noexcept
	{
		LOG_INFO("[MQTT] 收到【降落】指令, 准备执行...");
		plane::services::FlyManager::getInstance().land();
	}

	void LogicHandler::handleControlStrategySwitch(const n_json& payloadJson) noexcept
	{
		auto payload { payloadJson.get<plane::protocol::ControlStrategyPayload>() };
		if (payload.YTJSCL)
		{
			LOG_INFO("[MQTT] 收到【云台控制策略切换】指令, 策略代码: {}", *payload.YTJSCL);
			// TODO: 根据 *payload.YTJSCL (0,1,2) 映射到 C++ 枚举或类型
			// TODO: 调用 FlyManager 的 setControlStrategy(...), 并处理成功/失败回调
		}
	}

	void LogicHandler::handleCircleFly(const n_json& payloadJson) noexcept
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

	void LogicHandler::handleGimbalControl(const n_json& payloadJson) noexcept
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

	void LogicHandler::handleCameraControl(const n_json& payloadJson) noexcept
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

	void LogicHandler::handleStickData(const n_json& payloadJson) noexcept
	{
		// TODO: 在这里检查虚拟摇杆功能是否开启 (SettingsManager.isVirtualStickFeatureEnabled)
		[[maybe_unused]] auto payload { payloadJson.get<plane::protocol::StickDataPayload>() };
		// TODO: 调用 FlyManager 的 sendRawStickData(...);
	}

	void LogicHandler::handleStickModeSwitch(const n_json& payloadJson) noexcept
	{
		auto payload { payloadJson.get<plane::protocol::StickModeSwitchPayload>() };
		LOG_INFO("[MQTT] 收到【虚拟摇杆模式切换】指令: mode={}", payload.YGMS);
		// TODO: 根据 payload.stickMode (0,1,2) 调用 FlyManager 的 enable/disableVirtualStick, 并处理回调
	}

	void LogicHandler::handleNedVelocity(const n_json& payloadJson) noexcept
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
} // namespace plane::services

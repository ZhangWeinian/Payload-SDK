// raspberry_pi/services/MQTT/Handler/LogicHandler.cpp

#include "services/MQTT/Handler/LogicHandler.h"

#include <string_view>
#include <filesystem>
#include <functional>

#include "protocol/DroneDataClass.h"
#include "services/DroneControl/FlyManager.h"
#include "services/MQTT/Handler/MessageHandler.h"
#include "services/MQTT/Topics.h"
#include "utils/EnvironmentCheck.h"
#include "utils/JsonConverter/JsonToKmz.h"
#include "utils/Logger.h"

namespace plane::services
{
	using n_json = _NLOHMANN_JSON json;

	using namespace _STD		  literals;

	LogicHandler&				  LogicHandler::getInstance(void) noexcept
	{
		static LogicHandler instance {};
		return instance;
	}

	bool LogicHandler::init(void) noexcept
	{
		try
		{
			LOG_DEBUG("正在初始化 MQTT 业务逻辑处理器...");
			auto& msg_handler { MqttMessageHandler::getInstance() };

			msg_handler.registerHandler(plane::services::TOPIC_MISSION_CONTROL,
										"XFHXRW",
										_STD bind_front(&LogicHandler::handleWaypointMission, this));
			msg_handler.registerHandler(plane::services::TOPIC_COMMAND_CONTROL, "QF", _STD bind_front(&LogicHandler::handleTakeoff, this));
			msg_handler.registerHandler(plane::services::TOPIC_COMMAND_CONTROL, "FH", _STD bind_front(&LogicHandler::handleGoHome, this));
			msg_handler.registerHandler(plane::services::TOPIC_COMMAND_CONTROL, "XT", _STD bind_front(&LogicHandler::handleHover, this));
			msg_handler.registerHandler(plane::services::TOPIC_COMMAND_CONTROL, "JL", _STD bind_front(&LogicHandler::handleLand, this));
			msg_handler.registerHandler(plane::services::TOPIC_COMMAND_CONTROL,
										"YTJSCL",
										_STD bind_front(&LogicHandler::handleControlStrategySwitch, this));
			msg_handler.registerHandler(plane::services::TOPIC_COMMAND_CONTROL, "ZNHR", _STD bind_front(&LogicHandler::handleCircleFly, this));
			msg_handler.registerHandler(plane::services::TOPIC_PAYLOAD_CONTROL,
										"YTKZ",
										_STD bind_front(&LogicHandler::handleGimbalControl, this));
			msg_handler.registerHandler(plane::services::TOPIC_PAYLOAD_CONTROL,
										"BJKZ",
										_STD bind_front(&LogicHandler::handleCameraControl, this));
			msg_handler.registerHandler(plane::services::TOPIC_ROCKER_CONTROL, "YGFXZL", _STD bind_front(&LogicHandler::handleStickData, this));
			msg_handler.registerHandler(plane::services::TOPIC_ROCKER_CONTROL,
										"YGMSQH",
										_STD bind_front(&LogicHandler::handleStickModeSwitch, this));
			msg_handler.registerHandler(plane::services::TOPIC_VELOCITY_CONTROL,
										"SDKZ",
										_STD bind_front(&LogicHandler::handleNedVelocity, this));
		}
		catch (const _STD exception& e)
		{
			LOG_ERROR("初始化 MQTT 业务逻辑处理器时发生异常: {}", e.what());
			return false;
		}

		return true;
	}

#define HANDLE_PAYLOAD(command_name, payload_type, code_block)                                             \
	try                                                                                                    \
	{                                                                                                      \
		const auto payload { payloadJson.get<payload_type>() };                                            \
		code_block                                                                                         \
	}                                                                                                      \
	catch (const n_json::exception& e)                                                                     \
	{                                                                                                      \
		LOG_ERROR("解析【{}】指令失败: {}, payloadJson:\n{}", command_name, e.what(), payloadJson.dump()); \
	}

	void LogicHandler::handleWaypointMission(const n_json& payloadJson) noexcept
	{
		HANDLE_PAYLOAD("航线任务", plane::protocol::WaypointPayload, {
			if (plane::utils::isTestKmzFile())
			{
				LOG_INFO("测试指定航线 /tmp/kmz/1.kmz");
				if (_STD_FS exists("/tmp/kmz/1.kmz"))
				{
					plane::services::FlyManager::getInstance().waypoint("/tmp/kmz/1.kmz"sv);
				}
				else
				{
					LOG_ERROR("测试指定航线 /tmp/kmz/1.kmz 不存在");
				}
			}
			else
			{
				if (payload.HDJ.empty())
				{
					LOG_WARN("[MQTT] 收到的航点任务 (RWID: {}) 中不包含任何航点。", payload.RWID.value_or("N/A"));
					return;
				}

				if (payload.HDJ.size() == 1)
				{
					LOG_INFO("[MQTT] 收到并准备执行【单航点任务】");
					plane::services::FlyManager::getInstance().flyToPoint(payload.HDJ[0]);
				}
				else
				{
					LOG_INFO("[MQTT] 收到并准备执行【航线任务】, 共 {} 个航点", payload.HDJ.size());
					if (auto kmzData { plane::utils::JsonToKmzConverter::convertWaypointsToKmz(payload.HDJ, payload) }; kmzData)
					{
						plane::services::FlyManager::getInstance().waypoint(*kmzData);
					}
					else
					{
						LOG_ERROR("无法执行航线任务，因为 KMZ 数据生成失败。");
						return;
					}
				}
			}
		});
	}

	void LogicHandler::handleTakeoff(const n_json& payloadJson) noexcept
	{
		HANDLE_PAYLOAD("起飞", plane::protocol::TakeoffPayload, {
			LOG_INFO("[MQTT] 收到【起飞】指令");
			plane::services::FlyManager::getInstance().takeoff(payload);
		});
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
		LOG_INFO("[MQTT] 收到【降落】指令");
		plane::services::FlyManager::getInstance().land();
	}

	void LogicHandler::handleControlStrategySwitch(const n_json& payloadJson) noexcept
	{
		HANDLE_PAYLOAD("云台控制策略切换", plane::protocol::ControlStrategyPayload, {
			if (payload.YTJSCL)
			{
				LOG_INFO("[MQTT] 收到【云台控制策略切换】指令, 策略代码: {}", *payload.YTJSCL);
				// TODO: FlyManager::getInstance().setControlStrategy(*payload.YTJSCL);
			}
		});
	}

	void LogicHandler::handleCircleFly(const n_json& payloadJson) noexcept
	{
		HANDLE_PAYLOAD("智能环绕", plane::protocol::CircleFlyPayload, {
			LOG_INFO("[MQTT] 收到【智能环绕】指令");
			// TODO: FlyManager::getInstance().flyCircleAroundPoint(payload);
		});
	}

	void LogicHandler::handleGimbalControl(const n_json& payloadJson) noexcept
	{
		HANDLE_PAYLOAD("云台控制", plane::protocol::GimbalControlPayload, {
			if (payload.MS == 0) // 角度控制
			{
				LOG_INFO("[MQTT] 收到【云台角度控制】指令: pitch={}, yaw={}", payload.FYJ, payload.PHJ);
				// TODO: FlyManager::getInstance().rotateGimbal(payload.FYJ, payload.PHJ);
			}
			else // 速度控制
			{
				LOG_INFO("[MQTT] 收到【云台速度控制】指令: pitch={}, yaw={}", payload.FYJ, payload.PHJ);
				// TODO: FlyManager::getInstance().rotateGimbalBySpeed(payload.FYJ, payload.PHJ, .0);
			}
		});
	}

	void LogicHandler::handleCameraControl(const n_json& payloadJson) noexcept
	{
		HANDLE_PAYLOAD("相机控制", plane::protocol::ZoomControlPayload, {
			if (payload.BJB)
			{
				LOG_INFO("[MQTT] 收到【相机变焦】指令: factor={}", *payload.BJB);
				// TODO: FlyManager::getInstance().setCameraZoomFactor(*payload.BJB);
			}
			if (payload.XJLX)
			{
				LOG_INFO("[MQTT] 收到【相机视频源切换】指令: source={}", *payload.XJLX);
				// TODO: FlyManager::getInstance().setCameraStreamSource(*payload.XJLX);
			}
		});
	}

	void LogicHandler::handleStickData(const n_json& payloadJson) noexcept
	{
		HANDLE_PAYLOAD("虚拟摇杆数据",
					   plane::protocol::StickDataPayload,
					   {
						   // TODO: FlyManager::getInstance().sendRawStickData(payload);
					   });
	}

	void LogicHandler::handleStickModeSwitch(const n_json& payloadJson) noexcept
	{
		HANDLE_PAYLOAD("虚拟摇杆模式切换", plane::protocol::StickModeSwitchPayload, {
			LOG_INFO("[MQTT] 收到【虚拟摇杆模式切换】指令: mode={}", payload.YGMS);
			// TODO: FlyManager::getInstance().switchVirtualStick(payload.YGMS);
		});
	}

	void LogicHandler::handleNedVelocity(const n_json& payloadJson) noexcept
	{
		HANDLE_PAYLOAD("NED速度控制", plane::protocol::NedVelocityPayload, {
			LOG_DEBUG("[MQTT] 收到【NED速度控制】指令");
			// TODO: FlyManager::getInstance().sendNedVelocityCommand(payload);
		});
	}

#undef HANDLE_PAYLOAD
} // namespace plane::services

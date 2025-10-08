// raspberry_pi/services/MQTT/Handler/LogicHandler.cpp

#include "LogicHandler.h"

#include "config/ConfigManager.h"
#include "protocol/DroneDataClass.h"
#include "services/DroneControl/FlyManager.h"
#include "services/MQTT/Handler/MessageHandler.h"
#include "services/MQTT/Topics.h"
#include "utils/JsonConverter/JsonToKmz.h"
#include "utils/Logger.h"

#include <string_view>
#include <filesystem>
#include <functional>

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

	template<typename PayloadType, typename Func>
	void LogicHandler::handleCommand(_STD string_view command_name, const n_json& payloadJson, Func&& handler)
	{
		if constexpr (_STD is_same_v<PayloadType, _STD monostate>)
		{
			try
			{
				handler();
			}
			catch (const _STD exception& e)
			{
				LOG_ERROR("执行【{}】指令时发生异常: {}", command_name, e.what());
			}
		}
		else
		{
			try
			{
				handler(payloadJson.get<PayloadType>());
			}
			catch (const n_json::exception& e)
			{
				LOG_ERROR("解析【{}】指令失败: {}, payloadJson:\n{}", command_name, e.what(), payloadJson.dump(4));
			}
			catch (const _STD exception& e)
			{
				LOG_ERROR("执行【{}】指令时发生异常: {}", command_name, e.what());
			}
		}
	}

	void LogicHandler::handleWaypointMission(const n_json& payloadJson) noexcept
	{
		this->handleCommand<plane::protocol::WaypointPayload>(
			"航线任务",
			payloadJson,
			[&](const auto& payload)
			{
				if (plane::config::ConfigManager::getInstance().isTestKmzFile())
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
		this->handleCommand<plane::protocol::TakeoffPayload>("起飞",
															 payloadJson,
															 [&](const auto& payload)
															 {
																 LOG_INFO("[MQTT] 收到【起飞】指令");
																 plane::services::FlyManager::getInstance().takeoff(payload);
															 });
	}

	void LogicHandler::handleGoHome(const n_json& payloadJson) noexcept
	{
		this->handleCommand<_STD monostate>("返航",
											payloadJson,
											[&]()
											{
												LOG_INFO("[MQTT] 收到【返航】指令");
												plane::services::FlyManager::getInstance().goHome();
											});
	}

	void LogicHandler::handleHover(const n_json& payloadJson) noexcept
	{
		this->handleCommand<_STD monostate>("悬停",
											payloadJson,
											[&]()
											{
												LOG_INFO("[MQTT] 收到【悬停】指令");
												plane::services::FlyManager::getInstance().hover();
											});
	}

	void LogicHandler::handleLand(const n_json& payloadJson) noexcept
	{
		this->handleCommand<_STD monostate>("降落",
											payloadJson,
											[&]()
											{
												LOG_INFO("[MQTT] 收到【降落】指令");
												plane::services::FlyManager::getInstance().land();
											});
	}

	void LogicHandler::handleControlStrategySwitch(const n_json& payloadJson) noexcept
	{
		this->handleCommand<plane::protocol::ControlStrategyPayload>(
			"云台控制策略切换",
			payloadJson,
			[&](const auto& payload)
			{
				if (payload.YTJSCL)
				{
					LOG_INFO("[MQTT] 收到【云台控制策略切换】指令, 策略代码: {}", *payload.YTJSCL);
					plane::services::FlyManager::getInstance().setControlStrategy(*payload.YTJSCL);
				}
			});
	}

	void LogicHandler::handleCircleFly(const n_json& payloadJson) noexcept
	{
		this->handleCommand<plane::protocol::CircleFlyPayload>("智能环绕",
															   payloadJson,
															   [&](const auto& payload)
															   {
																   LOG_INFO("[MQTT] 收到【智能环绕】指令");
																   plane::services::FlyManager::getInstance().flyCircleAroundPoint(payload);
															   });
	}

	void LogicHandler::handleGimbalControl(const n_json& payloadJson) noexcept
	{
		this->handleCommand<plane::protocol::GimbalControlPayload>(
			"云台控制",
			payloadJson,
			[&](const auto& payload)
			{
				if (payload.MS == 0) // 角度控制
				{
					LOG_INFO("[MQTT] 收到【云台角度控制】指令: pitch={}, yaw={}", payload.FYJ, payload.PHJ);
					plane::services::FlyManager::getInstance().rotateGimbal(payload);
				}
				else // 速度控制
				{
					LOG_INFO("[MQTT] 收到【云台速度控制】指令: pitch={}, yaw={}", payload.FYJ, payload.PHJ);
					plane::services::FlyManager::getInstance().rotateGimbalBySpeed(payload);
				}
			});
	}

	void LogicHandler::handleCameraControl(const n_json& payloadJson) noexcept
	{
		this->handleCommand<plane::protocol::ZoomControlPayload>("相机控制",
																 payloadJson,
																 [&](const auto& payload)
																 {
																	 if (payload.BJB)
																	 {
																		 LOG_INFO("[MQTT] 收到【相机变焦】指令: factor={}", *payload.BJB);
																		 plane::services::FlyManager::getInstance().setCameraZoomFactor(payload);
																	 }
																	 if (payload.XJLX)
																	 {
																		 LOG_INFO("[MQTT] 收到【相机视频源切换】指令: source={}", *payload.XJLX);
																		 plane::services::FlyManager::getInstance().setCameraStreamSource(
																			 payload);
																	 }
																 });
	}

	void LogicHandler::handleStickData(const n_json& payloadJson) noexcept
	{
		this->handleCommand<plane::protocol::StickDataPayload>(
			"虚拟摇杆数据",
			payloadJson,
			[&](const auto& payload)
			{
				LOG_INFO("[MQTT] 收到【虚拟摇杆数据】指令: 油门量={}, 偏航量={}, 俯仰量={}, 横滚量={}",
						 payload.YML,
						 payload.PHL,
						 payload.FYL,
						 payload.HGL);

				plane::services::FlyManager::getInstance().sendRawStickData(payload);
			});
	}

	void LogicHandler::handleStickModeSwitch(const n_json& payloadJson) noexcept
	{
		this->handleCommand<plane::protocol::StickModeSwitchPayload>("虚拟摇杆模式切换",
																	 payloadJson,
																	 [&](const auto& payload)
																	 {
																		 LOG_INFO("[MQTT] 收到【虚拟摇杆模式切换】指令: mode={}", payload.YGMS);
																		 plane::services::FlyManager::getInstance().enableVirtualStick(
																			 payload.YGMS);
																	 });
	}

	void LogicHandler::handleNedVelocity(const n_json& payloadJson) noexcept
	{
		this->handleCommand<plane::protocol::NedVelocityPayload>(
			"NED速度控制",
			payloadJson,
			[&](const auto& payload)
			{
				LOG_INFO("[MQTT] 收到【NED速度控制】指令: 北向速度={}, 东向速度={}, 地向速度={}, 偏航角速率={}",
						 payload.SDN,
						 payload.SDD,
						 payload.SDX,
						 payload.PHJ);
				plane::services::FlyManager::getInstance().sendNedVelocityCommand(payload);
			});
	}
} // namespace plane::services

#include "services/mqtt/MqttMessageHandler/MqttMessageHandler.h"
#include "services/mqtt/MqttTestHandler/MqttTestHandler.h"
#include "services/mqtt/MqttTopics.h"
#include "utils/Logger/Logger.h"

#include "dji_fc_subscription.h"
#include "dji_flight_controller.h"

#include <flight_control/test_flight_control.h>
#include <interest_point/test_interest_point.h>
#include <waypoint_v2/test_waypoint_v2.h>
#include <waypoint_v3/test_waypoint_v3.h>

namespace plane::services
{
	namespace MqttTestHandler
	{
		using n_json = nlohmann::json;

		static bool s_hasJoystickAuthority { false };

		static void TestTakeoffCallback(T_DjiReturnCode returnCode) noexcept
		{
			if (returnCode == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
			{
				LOG_INFO("[MQTT] 飞机起飞成功！");
			}
			else
			{
				LOG_ERROR("[MQTT] 飞机起飞失败，错误码: {:#08x}", returnCode);
			}
		}

		static void TestLandingCallback(T_DjiReturnCode returnCode) noexcept
		{
			if (returnCode == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
			{
				LOG_INFO("[MQTT] 飞机降落成功！");
			}
			else
			{
				LOG_ERROR("[MQTT] 飞机降落失败，错误码: {:#08x}", returnCode);
			}
		}

		static void TestGoHomeCallback(T_DjiReturnCode returnCode) noexcept
		{
			if (returnCode == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
			{
				LOG_INFO("[MQTT] 飞机返航成功！");
			}
			else
			{
				LOG_ERROR("[MQTT] 飞机返航失败，错误码: {:#08x}", returnCode);
			}
		}

		void initialize(void) noexcept
		{
			LOG_INFO("正在初始化 MQTT [测试] 逻辑处理器...");
			auto&			msg_handler { MqttMessageHandler::getInstance() };

			T_DjiReturnCode returnCode {};
			returnCode = DjiFcSubscription_SubscribeTopic(DJI_FC_SUBSCRIPTION_TOPIC_STATUS_FLIGHT, DJI_DATA_SUBSCRIPTION_TOPIC_10_HZ, NULL);
			if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
			{
				LOG_ERROR("[MQTT] 订阅大疆飞控状态数据失败，错误码: {:#08x}", returnCode);
			}
			else
			{
				LOG_INFO("[MQTT] 订阅大疆飞控状态数据成功。");
			}

			returnCode = DjiFcSubscription_SubscribeTopic(DJI_FC_SUBSCRIPTION_TOPIC_STATUS_DISPLAYMODE, DJI_DATA_SUBSCRIPTION_TOPIC_10_HZ, NULL);
			if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
			{
				LOG_ERROR("[MQTT] 订阅大疆状态显示模式失败，错误码: {:#08x}", returnCode);
			}
			else
			{
				LOG_INFO("[MQTT] 订阅大疆状态显示模式成功。");
			}

			msg_handler.registerHandler(TOPIC_TEST,
										"qf",
										[](const n_json& payloadJson)
										{
											LOG_INFO("[MQTT] 收到【起飞】指令...");

											T_DjiReturnCode rc {};

											// 步骤 2.1: 获取控制权
											if (!s_hasJoystickAuthority)
											{
												LOG_INFO("[MQTT] 正在获取摇杆控制权...");
												rc = DjiFlightController_ObtainJoystickCtrlAuthority();
												if (rc != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
												{
													LOG_ERROR("[MQTT] 获取控制权失败，无法起飞。错误码: {:#08x}", rc);
													return;
												}
												LOG_INFO("[MQTT] 成功获取控制权。");
												s_hasJoystickAuthority = true;
											}

											// 步骤 2.2: 发送起飞指令
											LOG_INFO("[MQTT] 正在发送起飞指令...");
											rc = DjiFlightController_StartTakeoff();
											if (rc != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
											{
												LOG_ERROR("[MQTT] 发送起飞指令失败。错误码: {:#08x}", rc);
											}
											else
											{
												LOG_INFO("[MQTT] 起飞指令已成功发送至飞控。");
											}
										});

			msg_handler.registerHandler(TOPIC_TEST,
										"jl",
										[](const n_json& payloadJson)
										{
											LOG_INFO("[MQTT] 收到【降落】指令...");
											T_DjiReturnCode rc = DjiFlightController_StartLanding();
											if (rc != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
											{
												LOG_ERROR("[MQTT] 发送降落指令失败。错误码: {:#08x}", rc);
											}
											else
											{
												LOG_INFO("[MQTT] 降落指令已成功发送至飞控。");
											}

											// 降落后，通常控制权会自动交还，但为了保险起见，我们在这里也释放一下
											if (s_hasJoystickAuthority)
											{
												LOG_INFO("[MQTT] 正在释放摇杆控制权...");
												DjiFlightController_ReleaseJoystickCtrlAuthority();
												s_hasJoystickAuthority = false;
											}
										});

			msg_handler.registerHandler(TOPIC_TEST,
										"fh",
										[](const n_json& payloadJson)
										{
											LOG_INFO("[MQTT] 收到【返航】指令...");
											T_DjiReturnCode rc = DjiFlightController_StartGoHome();
											if (rc != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
											{
												LOG_ERROR("[MQTT] 发送返航指令失败。错误码: {:#08x}", rc);
											}
											else
											{
												LOG_INFO("[MQTT] 返航指令已成功发送至飞控。");
											}
										});

			LOG_INFO("MQTT [测试] 逻辑处理器注册完毕。");
		}
	} // namespace MqttTestHandler
} // namespace plane::services

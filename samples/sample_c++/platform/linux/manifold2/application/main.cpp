#include "application.hpp"

#include "camera_manager/test_camera_manager_entry.h"
#include "data_transmission/test_data_transmission.h"
#include "fc_subscription/test_fc_subscription.h"
#include "widget/test_widget.h"
#include "widget/test_widget_speaker.h"

#include <camera_emu/test_payload_cam_emu_base.h>
#include <camera_emu/test_payload_cam_emu_media.h>
#include <dji_logger.h>
#include <flight_control/test_flight_control.h>
#include <flight_controller/test_flight_controller_entry.h>
#include <gimbal/test_gimbal_entry.hpp>
#include <gimbal_emu/test_payload_gimbal_emu.h>
#include <hms_manager/hms_manager_entry.h>
#include <liveview/test_liveview_entry.hpp>
#include <perception/test_lidar_entry.hpp>
#include <perception/test_perception_entry.hpp>
#include <perception/test_radar_entry.hpp>
#include <positioning/test_positioning.h>
#include <power_management/test_power_management.h>

#include "config/ConfigManager.h"
#include "protocol/JsonProtocol.h"
#include "services/mqtt/MqttLogicHandler/MqttLogicHandler.h"
#include "services/mqtt/MqttMessageHandler/MqttMessageHandler.h"
#include "services/mqtt/MqttService/MqttService.h"
#include "services/mqtt/MqttTestHandler/MqttTestHandler.h"
#include "services/mqtt/MqttTopics.h"
#include "services/telemetry/TelemetryReporter.h"
#include "utils/JsonConverter/JsonConverter.h"
#include "utils/Logger/Logger.h"
#include "utils/NetworkUtils/NetworkUtils.h"

#include "CLI11/CLI/CLI.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

#define RUN_STANDALONE_MQTT_APP

std::atomic<bool> g_should_exit(false);

void			  signalHandler(int signum)
{
	std::cout << "捕获到信号 " << signum << ", 正在准备退出..." << std::endl;
	g_should_exit = true;
}

void runDJIApplication(int argc, char** argv)
{
	char						   inputChar {};
	T_DjiOsalHandler*			   osalHandler = DjiPlatform_GetOsalHandler();
	T_DjiReturnCode				   returnCode {};
	T_DjiTestApplyHighPowerHandler applyHighPowerHandler {};

start:
	std::cout << "\n"
			  << "| 可用命令:\n"
			  << "| [0] 飞控数据订阅示例 - 订阅四元数和 GPS 数据\n"
			  << "| [1] 飞行控制器示例 - 通过 PSDK 控制飞行\n"
			  << "| [2] 健康管理系统信息示例 - 按语言获取健康管理系统信息\n"
			  << "| [a] 云台管理器示例 - 通过 PSDK 控制云台\n"
			  << "| [c] 相机码流查看示例 - 显示相机视频流\n"
			  << "| [d] 双目视觉查看示例 - 显示双目图像\n"
			  << "| [e] 运行相机管理器示例 - 交互式地测试相机功能\n"
			  << "| [f] 启动 RTK 定位示例 - 当 RTK 信号正常时，接收 RTK RTCM 数据\n"
			  << "| [g] 请求激光雷达数据示例 - 请求激光雷达数据并将点云数据存储为 pcd 文件\n"
			  << "| [h] 请求毫米波雷达数据示例 - 请求毫米波雷达数据\n"
			  << std::endl;

	std::cin >> inputChar;
	switch (inputChar)
	{
		case '0':
		{
			DjiTest_FcSubscriptionRunSample();
			break;
		}
		case '1':
		{
			DjiUser_RunFlightControllerSample();
			break;
		}
		case '2':
		{
			DjiUser_RunHmsManagerSample();
			break;
		}
		case 'a':
		{
			DjiUser_RunGimbalManagerSample();
			break;
		}
		case 'c':
		{
			DjiUser_RunCameraStreamViewSample();
			break;
		}
		case 'd':
		{
			DjiUser_RunStereoVisionViewSample();
			break;
		}
		case 'e':
		{
			DjiUser_RunCameraManagerSample();
			break;
		}
		case 'f':
		{
			returnCode = DjiTest_PositioningStartService();
			if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
			{
				USER_LOG_ERROR("RTK 定位样本初始化错误");
			}
			else
			{
				USER_LOG_INFO("成功启动 RTK 定位样本");
			}
			break;
		}
		case 'g':
		{
			DjiUser_RunLidarDataSubscriptionSample();
			break;
		}
		case 'h':
		{
			DjiUser_RunRadarDataSubscriptionSample();
			break;
		}
		default:
		{
			break;
		}
	}

	osalHandler->TaskSleepMs(2000);

	goto start;
}

void runMyApplication(int argc, char** argv)
{
	LOG_INFO("==========================================================");
	LOG_INFO("                      应用程序启动中");
	LOG_INFO("==========================================================");

	plane::utils::Logger::getInstance().init();

	if (!plane::config::ConfigManager::getInstance().load("config.yml"))
	{
		LOG_ERROR("致命错误: 无法加载配置文件 'config.yml'。正在退出。");
		return;
	}
	LOG_INFO("配置文件加载成功。");

	plane::services::MQTTService::getInstance();
	LOG_INFO("MQTT 服务已启动。");

	plane::services::initialize();
	plane::services::MqttTestHandler::initialize();
	LOG_INFO("业务逻辑处理器注册完毕。");

	LOG_INFO("正在启动遥测上报服务...");
	plane::services::TelemetryReporter::getInstance().start();

	LOG_INFO("==========================================================");
	LOG_INFO("               应用程序初始化完成，正在运行中...");
	LOG_INFO("                    按 Ctrl+C 退出。");
	LOG_INFO("==========================================================");

	while (!g_should_exit)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	LOG_INFO("收到退出信号，正在关闭应用程序...");

	plane::services::MQTTService::getInstance().shutdown();
	LOG_INFO("应用程序已关闭。");
}

int main(int argc, char** argv)
{
	signal(SIGINT, signalHandler);
	signal(SIGTERM, signalHandler);

	CLI::App app { "DJI PSDK 应用，集成自定义 MQTT 服务" };
	app.set_help_all_flag("--help-all", "显示所有帮助信息");

	bool runDjiInteractiveMode = false;
	app.add_flag("-d,--dji-interactive", runDjiInteractiveMode, "运行 DJI 官方的交互式示例菜单");

	CLI11_PARSE(app, argc, argv);

	if (runDjiInteractiveMode)
	{
		Application application(argc, argv);
		runDJIApplication(argc, argv);
	}
	else
	{
		runMyApplication(argc, argv);
	}

	LOG_INFO("程序执行完毕，正常退出。");
	return 0;
}

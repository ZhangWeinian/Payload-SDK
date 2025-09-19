// #define OPERATING_STANDARD_PROCEDURES

#include "application.hpp"

#include <dji_flight_controller.h>

#include "config/ConfigManager.h"
#include "dji_demo.h"
#include "services/mqtt/MqttHandler/MqttLogicHandler.h"
#include "services/mqtt/MqttService/MqttService.h"
#include "services/telemetry/TelemetryReporter.h"
#include "utils/Logger/Logger.h"

#include "CLI/CLI.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

std::atomic<bool> g_should_exit(false);

void			  signalHandler(int signum)
{
	std::cout << "捕获到信号 " << signum << ", 正在准备退出..." << std::endl;
	g_should_exit = true;
}

void runMyApplication(int argc, char** argv)
{
	LOG_INFO("==========================================================");
	LOG_INFO("                          应用程序启动中");
	LOG_INFO("==========================================================");

#ifdef OPERATING_STANDARD_PROCEDURES
	Application application(argc, argv);

	if (T_DjiReturnCode returnCode { DjiFlightController_SetRCLostActionEnableStatus(DJI_FLIGHT_CONTROLLER_DISABLE_RC_LOST_ACTION) };
		returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
	{
		LOG_WARN("禁用 RC 丢失动作失败，无法支持无遥控器飞行，错误码: 0x{:08X}", returnCode);
	}
	else
	{
		LOG_INFO("禁用 RC 成功");
	}
#else
	LOG_WARN("未启用标准作业流程，无法支持无遥控器飞行");
#endif

	if (!plane::config::ConfigManager::getInstance().load("config.yml"))
	{
		LOG_ERROR("致命错误: 无法加载配置文件 'config.yml'。正在退出。");
		return;
	}
	LOG_INFO("配置文件加载成功。");

	plane::services::MQTTService::getInstance();
	LOG_INFO("MQTT 服务已启动。");

	plane::services::initialize();
	LOG_INFO("业务逻辑处理器注册完毕。");

	LOG_INFO("正在启动遥测上报服务...");
	plane::services::TelemetryReporter::getInstance().start();

	std::this_thread::sleep_for(std::chrono::milliseconds(1500));
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
	plane::utils::Logger::getInstance().init(spdlog::level::info);

	signal(SIGINT, signalHandler);
	signal(SIGTERM, signalHandler);

	CLI::App app { "DJI PSDK 应用，集成自定义 MQTT 服务" };
	app.set_help_all_flag("--help-all", "显示所有帮助信息");

	bool runDjiInteractiveMode = false;
	app.add_flag("-d,--dji-interactive", runDjiInteractiveMode, "运行 DJI 官方的交互式示例菜单");

	CLI11_PARSE(app, argc, argv);

	if (runDjiInteractiveMode)
	{
		plane::dji_demo::runDjiApplication(argc, argv);
	}
	else
	{
		runMyApplication(argc, argv);
	}

	LOG_INFO("程序执行完毕，正常退出。");
	return 0;
}

// raspberry_pi/application/my_dji.h

#pragma once

#include "config/ConfigManager.h"
#include "services/MQTT/Handler/LogicHandler.h"
#include "services/MQTT/Service.h"
#include "services/PSDK/PSDKAdapter/PSDKAdapter.h"
#include "services/PSDK/PSDKManager/PSDKManager.h"
#include "services/Telemetry/TelemetryReporter.h"
#include "utils/DjiErrorUtils.h"
#include "utils/Logger.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <string>
#include <thread>

#include "define.h"

namespace plane::my_dji
{
	namespace
	{
		std::atomic<bool> g_should_exit(false);

		void			  signalHandler(int signum)
		{
			LOG_DEBUG("捕获到信号 {}, 正在准备退出...", signum);
			g_should_exit = true;
		}
	} // namespace

	void runMyApplication(int argc, char** argv)
	{
		STD_PRINTLN("==========================================================");
		STD_PRINTLN("                        应用程序启动中");
		STD_PRINTLN("==========================================================");

		_CSTD signal(SIGINT, _UNNAMED signalHandler);
		_CSTD signal(SIGTERM, _UNNAMED signalHandler);

		if (!plane::config::ConfigManager::getInstance().loadAndCheck("config.yml"))
		{
			LOG_ERROR("错误: 配置文件加载失败，程序退出。");
			return;
		}
		LOG_INFO("配置文件加载成功。");

		if (plane::config::ConfigManager::getInstance().isLogLevelDebug())
		{
			plane::utils::Logger::getInstance().init(_SPDLOG level::trace);
		}
		else
		{
			plane::utils::Logger::getInstance().init(_SPDLOG level::info);
		}

		if (plane::config::ConfigManager::getInstance().isStandardProceduresEnabled())
		{
			if (!plane::services::PSDKManager::getInstance().initialize(argc, argv))
			{
				LOG_ERROR("PSDK 底层服务初始化失败，程序退出。");
				return;
			}
			LOG_INFO("PSDK 底层服务初始化完毕。");
		}
		else
		{
			LOG_WARN("未启用标准 PSDK 作业流程 (环境变量 FULL_PSDK=1 未设置)。");
		}

		if (!plane::services::MQTTService::getInstance().start())
		{
			LOG_ERROR("错误: MQTT 服务启动失败，程序退出。");
			return;
		}
		LOG_INFO("MQTT 服务已启动。");

		if (!plane::services::LogicHandler::getInstance().init())
		{
			LOG_ERROR("错误: 业务逻辑处理器初始化失败，程序退出。");
			return;
		}
		LOG_INFO("业务逻辑处理器注册完毕。");

		if (!plane::services::PSDKAdapter::getInstance().start())
		{
			LOG_ERROR("PSDK 适配器运行时线程启动失败！");
			return;
		}
		LOG_INFO("PSDK 适配器运行时线程已启动。");

		if (!plane::services::TelemetryReporter::getInstance().start())
		{
			LOG_ERROR("错误: 遥测上报服务启动失败，程序退出。");
			return;
		}
		LOG_INFO("遥测上报服务已启动。");

		LOG_INFO("==========================================================");
		LOG_INFO("               应用程序初始化完成, 正在运行中...");
		LOG_INFO("                    按 Ctrl+C 退出。");
		LOG_INFO("==========================================================");

		while (!g_should_exit)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
		}

		LOG_INFO("收到退出信号, 正在关闭应用程序...");

		plane::services::TelemetryReporter::getInstance().stop();
		LOG_INFO("遥测上报服务已停止。");

		plane::services::PSDKAdapter::getInstance().stop();
		LOG_INFO("PSDK 适配器运行时线程已停止。");

		plane::services::MQTTService::getInstance().stop();
		LOG_INFO("MQTT 服务已停止。");

		if (plane::config::ConfigManager::getInstance().isStandardProceduresEnabled())
		{
			plane::services::PSDKManager::getInstance().deinitialize();
		}

		std::this_thread::sleep_for(std::chrono::seconds(1));
		LOG_INFO("应用程序已关闭。");
	}
} // namespace plane::my_dji

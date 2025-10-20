// cy_psdk/workspace/my_dji.h

#pragma once

#include "config/ConfigManager.h"
#include "services/Heartbeat/Heartbeat.h"
#include "services/MQTT/Handler/LogicHandler.h"
#include "services/MQTT/Service.h"
#include "services/PSDK/PSDKAdapter.h"
#include "services/PSDK/PSDKManager.h"
#include "services/Telemetry/TelemetryReporter.h"
#include "utils/Logger.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#include "define.h"

namespace plane::my_dji
{
	namespace
	{
		_STD atomic<bool> g_should_exit(false);

		void			  signalHandler(int signum)
		{
			LOG_DEBUG("捕获到信号 {}, 正在准备退出...", signum);
			g_should_exit = true;
		}
	} // namespace

	void runMyApplication(int argc, char* argv[])
	{
		// 注册信号处理函数
		_CSTD signal(SIGINT, _UNNAMED signalHandler);
		_CSTD signal(SIGTERM, _UNNAMED signalHandler);

		// 日志系统初始化（必须最先初始化）
		plane::utils::Logger::getInstance().init();

		LOG_INFO("==========================================================");
		LOG_INFO("                        应用程序启动中");
		LOG_INFO("==========================================================");

		auto& config { plane::config::ConfigManager::getInstance() };

		// 尝试加载配置文件
		if (!config.loadAndCheck())
		{
			LOG_ERROR("错误: 配置文件加载失败，程序退出。");
			return;
		}

		// 根据配置设置日志级别
		if (config.isLogLevelDebug())
		{
			plane::utils::Logger::getInstance().setLocalLogFileLevel(_SPDLOG level::trace);
		}
		else
		{
			plane::utils::Logger::getInstance().setLocalLogFileLevel(_SPDLOG level::info);
		}

		// 如果启用标准 PSDK 作业流程，则初始化 PSDKManager 和 PSDKAdapter
		if (config.isStandardProceduresEnabled())
		{
			if (!plane::services::PSDKManager::getInstance().start(argc, argv))
			{
				LOG_ERROR("PSDK 底层服务初始化失败，程序退出。");
				return;
			}

			if (!plane::services::PSDKAdapter::getInstance().start())
			{
				LOG_ERROR("PSDK 适配器运行时启动失败！");
				return;
			}
		}
		else
		{
			LOG_WARN("未启用标准 PSDK 作业流程。");
		}

		// 尝试启动 MQTT 服务
		if (!plane::services::MQTTService::getInstance().start())
		{
			LOG_ERROR("错误: MQTT 服务启动失败，程序退出。");
			return;
		}

		// 尝试启动心跳服务
		if (!plane::services::Heartbeat::getInstance().start())
		{
			LOG_ERROR("错误: 心跳服务启动失败，程序退出。");
			return;
		}

		// 尝试初始化业务逻辑处理器
		if (!plane::services::LogicHandler::getInstance().init())
		{
			LOG_ERROR("错误: 业务逻辑处理器初始化失败，程序退出。");
			return;
		}

		// 尝试启动遥测上报服务
		if (!plane::services::TelemetryReporter::getInstance().start())
		{
			LOG_ERROR("错误: 遥测上报服务启动失败，程序退出。");
			return;
		}

		// 等待一段时间让各服务稳定运行，随后报告应用已启动
		_STD this_thread::sleep_for(_STD_CHRONO seconds(2));
		LOG_INFO("==========================================================");
		LOG_INFO("               应用程序初始化完成, 正在运行中...");
		LOG_INFO("                    按 Ctrl+C 退出。");
		LOG_INFO("==========================================================");

		while (!g_should_exit)
		{
			_STD this_thread::sleep_for(_STD_CHRONO milliseconds(500));
		}

		// 收到退出信号，开始关闭各服务
		LOG_INFO("收到退出信号, 正在关闭应用程序...");

		plane::services::TelemetryReporter::getInstance().stop();
		plane::services::Heartbeat::getInstance().stop();
		plane::services::MQTTService::getInstance().stop();

		if (config.isStandardProceduresEnabled())
		{
			plane::services::PSDKManager::getInstance().stop();
			plane::services::PSDKAdapter::getInstance().stop();
		}

		_STD this_thread::sleep_for(_STD_CHRONO seconds(1));
		LOG_INFO("应用程序已关闭。");
	}
} // namespace plane::my_dji

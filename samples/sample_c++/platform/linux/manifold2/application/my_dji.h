#pragma once

#include "application.hpp"

#include <dji_flight_controller.h>

#include "config/ConfigManager.h"
#include "services/mqtt/Handler/LogicHandler.h"
#include "services/mqtt/Service.h"
#include "services/telemetry/TelemetryReporter.h"
#include "utils/Logger/Logger.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <string>
#include <thread>

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

		bool isStandardProceduresEnabled(void) noexcept
		{
			const char* envValue { std::getenv("FULL_PSDK") };
			return (envValue != nullptr) && (std::string(envValue) == "1");
		}

		bool isLogLevelDebug(void) noexcept
		{
			const char* envValue { std::getenv("LOG_DEBUG") };
			return (envValue != nullptr) && (std::string(envValue) == "1");
		}
	} // namespace

	void runMyApplication(int argc, char** argv)
	{
		signal(SIGINT, signalHandler);
		signal(SIGTERM, signalHandler);

		if (isLogLevelDebug())
		{
			plane::utils::Logger::getInstance().init(spdlog::level::trace);
			LOG_DEBUG("日志级别设置为 DEBUG");
		}
		else
		{
			plane::utils::Logger::getInstance().init(spdlog::level::info);
			LOG_INFO("日志级别设置为 INFO");
			LOG_INFO("如需调试日志，请设置环境变量: export LOG_DEBUG=1");
		}

		LOG_INFO("==========================================================");
		LOG_INFO("                        应用程序启动中");
		LOG_INFO("==========================================================");

		if (isStandardProceduresEnabled())
		{
			Application application(argc, argv);

			if (T_DjiReturnCode returnCode { DjiFlightController_SetRCLostActionEnableStatus(DJI_FLIGHT_CONTROLLER_DISABLE_RC_LOST_ACTION) };
				returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
			{
				LOG_WARN("禁用 RC 失败，无法支持无遥控器飞行，错误码: 0x{:08X}", returnCode);
			}
			else
			{
				LOG_INFO("禁用 RC 成功");
			}
		}
		else
		{
			LOG_WARN("未启用标准作业流程，无法支持无遥控器飞行。如需启用，请设置环境变量: export FULL_PSDK=1");
		}

		if (plane::config::ConfigManager::getInstance().loadAndCheck("config.yml"))
		{
			LOG_INFO("配置文件加载成功: config.yml");
		}
		else
		{
			LOG_ERROR("错误: 配置文件加载失败，请检查 'config.yml' 是否存在且格式正确。");
			return;
		}

		if (plane::services::MQTTService::getInstance().start())
		{
			LOG_INFO("MQTT 服务已启动。");
		}
		else
		{
			LOG_ERROR("错误: MQTT 服务启动失败，请检查配置文件中的 MQTT 设置是否正确。");
			return;
		}

		if (plane::services::LogicHandler::init())
		{
			LOG_INFO("业务逻辑处理器注册完毕。");
		}
		else
		{
			LOG_ERROR("错误: 业务逻辑处理器初始化失败。");
			return;
		}

		if (plane::services::TelemetryReporter::getInstance().start())
		{
			LOG_INFO("遥测上报服务已启动。");
		}
		else
		{
			LOG_ERROR("错误: 遥测上报服务启动失败。");
			return;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(1500));
		LOG_INFO("==========================================================");
		LOG_INFO("               应用程序初始化完成，正在运行中...");
		LOG_INFO("                    按 Ctrl+C 退出。");
		LOG_INFO("==========================================================");

		while (!g_should_exit)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
		}
		LOG_INFO("收到退出信号，正在关闭应用程序...");

		plane::services::TelemetryReporter::getInstance().stop();

		plane::services::MQTTService::getInstance().stop();

		LOG_INFO("应用程序已关闭。");
	}
} // namespace plane::my_dji

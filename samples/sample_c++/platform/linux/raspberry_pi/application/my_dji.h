// raspberry_pi/application/my_dji.h

#pragma once

#include "application.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <string>
#include <thread>

#include <dji_flight_controller.h>

#include "config/ConfigManager.h"
#include "services/DroneControl/PSDKAdapter/PSDKAdapter.h"
#include "services/MQTT/Handler/LogicHandler.h"
#include "services/MQTT/Service.h"
#include "services/Telemetry/TelemetryReporter.h"
#include "utils/DjiErrorUtils.h"
#include "utils/EnvironmentCheck.h"
#include "utils/Logger.h"

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

	void runMyApplication(int argc, char** argv)
	{
		_CSTD signal(SIGINT, signalHandler);
		_CSTD signal(SIGTERM, signalHandler);

		if (plane::utils::isLogLevelDebug())
		{
			plane::utils::Logger::getInstance().init(_SPDLOG level::trace);
			LOG_DEBUG("日志级别设置为 DEBUG");
		}
		else
		{
			plane::utils::Logger::getInstance().init(_SPDLOG level::info);
			LOG_DEBUG("日志级别设置为 INFO");
			LOG_DEBUG("如需调试日志, 请设置环境变量: export LOG_DEBUG=1");
		}

		LOG_INFO("==========================================================");
		LOG_INFO("                        应用程序启动中");
		LOG_INFO("==========================================================");

		if (plane::utils::isStandardProceduresEnabled())
		{
			Application application(argc, argv);
			_STD		this_thread::sleep_for(_STD chrono::seconds(5));
			LOG_INFO("DJI PSDK 核心服务初始化完毕。");

			if (!plane::services::PSDKAdapter::getInstance().setup())
			{
				LOG_ERROR("PSDK 适配器准备失败！程序将无法获取飞机数据。");
				return;
			}
			else
			{
				LOG_INFO("PSDK 适配器准备就绪。");
			}

			if (!plane::services::PSDKAdapter::getInstance().start())
			{
				LOG_ERROR("PSDK 适配器数据采集线程启动失败！");
				return;
			}
			else
			{
				LOG_INFO("PSDK 适配器数据采集线程已启动。");
			}

			if (plane::utils::isSkipRC())
			{
				if (_DJI T_DjiReturnCode returnCode {
						_DJI DjiFlightController_SetRCLostActionEnableStatus(_DJI DJI_FLIGHT_CONTROLLER_DISABLE_RC_LOST_ACTION) };
					returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
				{
					LOG_WARN("禁用 RC 失败, 无法支持无遥控器飞行, 错误: {}, 错误码: 0x{:08X}",
							 plane::utils::djiReturnCodeToString(returnCode),
							 returnCode);
				}
				else
				{
					LOG_INFO("禁用 RC 成功");
				}
			}
			else
			{
				LOG_INFO("启用标准作业流程, 但没有禁用 RC");
			}
		}
		else
		{
			LOG_WARN("未启用标准作业流程, 无法支持无遥控器飞行。如需启用, 请设置环境变量: export FULL_PSDK=1");
		}

		if (plane::config::ConfigManager::getInstance().loadAndCheck("config.yml"))
		{
			LOG_INFO("配置文件加载成功: config.yml");
		}
		else
		{
			LOG_ERROR("错误: 配置文件加载失败, 请检查 'config.yml' 是否存在且格式正确。");
			return;
		}

		if (plane::services::MQTTService::getInstance().start())
		{
			LOG_INFO("MQTT 服务已启动。");
		}
		else
		{
			LOG_ERROR("错误: MQTT 服务启动失败, 请检查配置文件中的 MQTT 设置是否正确。");
			return;
		}

		if (plane::services::LogicHandler::getInstance().init())
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

		_STD this_thread::sleep_for(_STD_CHRONO seconds(1));
		LOG_INFO("==========================================================");
		LOG_INFO("               应用程序初始化完成, 正在运行中...");
		LOG_INFO("                    按 Ctrl+C 退出。");
		LOG_INFO("==========================================================");

		while (!g_should_exit)
		{
			_STD this_thread::sleep_for(_STD_CHRONO milliseconds(500));
		}
		LOG_INFO("收到退出信号, 正在关闭应用程序...");

		plane::services::TelemetryReporter::getInstance().stop();
		LOG_DEBUG("遥测上报服务已停止。");

		plane::services::MQTTService::getInstance().stop();
		LOG_DEBUG("MQTT 服务已停止。");

		if (plane::utils::isStandardProceduresEnabled())
		{
			plane::services::PSDKAdapter::getInstance().stop();
			plane::services::PSDKAdapter::getInstance().cleanup();
			LOG_DEBUG("PSDK 适配器已清理。");
		}

		_STD this_thread::sleep_for(_STD_CHRONO seconds(1));
		LOG_INFO("应用程序已关闭。");
	}
} // namespace plane::my_dji

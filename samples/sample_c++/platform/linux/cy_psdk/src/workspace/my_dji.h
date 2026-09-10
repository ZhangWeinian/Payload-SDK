// cy_psdk/workspace/my_dji.h

#pragma once

#include "application.hpp"

#include "config/ConfigManager.h"
#include "manager/binding/DeviceBinder.h"
#include "manager/catalog/CatalogManager.h"
#include "manager/heartbeat/Heartbeat.h"
#include "manager/mqtt/handler/LogicHandler.h"
#include "manager/mqtt/service/MQTTv5Service.h"
#include "manager/plane_state/PlaneStateStore.h"
#include "manager/psdk/PSDKAdapter.h"
#include "manager/psdk/PSDKManager.h"
#include "manager/status_board/StatusBoardManager.h"
#include "manager/telemetry/TelemetryReporter.h"
#include "manager/websocket/WsClient.h"
#include "utils/EXEHomePath.h"
#include "utils/integrity/IntegrityCheck.h"
#include "utils/log_util/Logger.h"
#include "utils/status_board/StatusBoard.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "define.h"

namespace plane::my_dji
{
	namespace
	{
		_STD atomic<bool> g_should_exit(false);

		// 信号处理器: 仅做 async-signal-safe 的原子置位。
		// 注意: 不可在信号上下文调用日志 (spdlog 非异步信号安全, 且与工作线程共享
		// 内部互斥量, 信号打断持锁代码时会造成死锁); 退出提示由主循环打印。
		void signalHandler(int /*signum*/)
		{
			g_should_exit = true;
		}
	} // namespace

	int runMyApplication(int argc, char* argv[])
	{
		// 以 argv[0] 确定交付目录 (config.yml/日志/libs 均相对它; loader 显式启动时 /proc/self/exe 不可靠)
		plane::utils::getEXEHomePath.init(argv[0]);

		// 持有 DJI Application 实例，确保其生命周期贯穿整个应用程序运行期间
		_STD unique_ptr<_DJI Application> PSDK_application_ptr_ { nullptr };

		// 日志系统初始化（必须最先初始化）
		plane::utils::Logger::getInstance().init();

		LOG_INFO("==========================================================");
		LOG_INFO("                        应用程序启动中");
		LOG_INFO("==========================================================");

		// 部署完整性自检: 校验 cy_psdk 与 libs/ 的 SHA256 (纯程序内实现, 不依赖板端外部工具)
		if (!plane::utils::verifyDeploymentIntegrity(argv[0]))
		{
			LOG_ERROR("部署完整性校验失败, 拒绝启动 (如需临时跳过请设置 CY_PSDK_SKIP_INTEGRITY=1)");
			return 2;
		}

		auto& config { plane::config::ConfigManager::getInstance() };

		// 尝试加载配置文件
		if (!config.loadAndCheck())
		{
			LOG_ERROR("错误: 配置文件加载失败，程序退出");
			return 1;
		}

		// 根据配置设置日志级别
		if (config.isTraceLogLevel())
		{
			plane::utils::Logger::getInstance().setLocalLogFileLevel(_SPDLOG level::trace);
		}
		else
		{
			plane::utils::Logger::getInstance().setLocalLogFileLevel(_SPDLOG level::info);
		}

		plane::domain::PlaneStateStore::getInstance().update(
			[&config](plane::domain::PlaneStateDataClass& st)
			{
				if (!config.isStandardProceduresEnabled())
				{
					st.serial_number = _STD string { config.getPlaneCode() };
				}
				st.swarm_agent_identifier = config.getCatalogServiceId();
				st.app_version			  = config.getCatalogVersion();
			}
		);

		// SwarmCatalog 目录客户端: 后台启动发现/注册, 与 PSDK 初始化并行, 不阻塞主链路
		plane::manager::CatalogManager::getInstance().start();

		// 设备绑定: 目录就绪 + 序列号就绪后自动绑定 (对齐 msdk), 后台执行
		plane::manager::DeviceBinder::getInstance().start();

		// WebSocket 数据订阅: 目录就绪后连接并订阅 (对齐 msdk), 后台执行
		plane::manager::WsClient::getInstance().start();

		// 如果启用标准 PSDK 作业流程，则初始化 PSDKManager 和 PSDKAdapter
		if (config.isStandardProceduresEnabled())
		{
			LOG_INFO("已启用标准 PSDK 作业流程");

			// 初始化 DJI Application
			try
			{
				LOG_INFO("初始化 PSDK CORE , 请等待");
				PSDK_application_ptr_ = _STD make_unique<_DJI Application>(argc, argv);
				_STD						 this_thread::sleep_for(_STD_CHRONO seconds(5));
			}
			catch (const _STD exception& e)
			{
				LOG_ERROR("PSDK CORE 初始化异常: {}", e.what());
				return 1;
			}
			catch (...)
			{
				LOG_ERROR("PSDK CORE 初始化发生未知异常: <non-std exception>");
				return 1;
			}

			// 尝试启动 PSDK 底层服务
			if (!plane::manager::PSDKManager::getInstance().start(argc, argv))
			{
				LOG_ERROR("PSDK 底层服务初始化失败，程序退出");
				return 1;
			}
			else
			{
				LOG_DEBUG("PSDK 底层服务已成功启动");
			}

			// 尝试启动 PSDK 适配器服务
			if (!plane::manager::PSDKAdapter::getInstance().start())
			{
				LOG_ERROR("PSDK 适配器运行时启动失败！");
				return 1;
			}
			else
			{
				LOG_DEBUG("PSDK 适配器已成功启动");
			}
		}
		else
		{
			LOG_WARN("未启用标准 PSDK 作业流程");
		}

		// 到达此处说明 PSDK 流程已就绪 (或未启用), 向目录组件状态上报标记就绪
		plane::manager::CatalogManager::getInstance().notifyPsdkRunning(true);

		// 尝试启动 MQTT 服务 (自治运行: 启动失败不退出, 由自检线程持续重试)
		if (!plane::manager::MQTTv5Service::getInstance().start())
		{
			LOG_ERROR("错误: MQTT 服务启动失败 (程序继续运行, 自检线程将重试)");
		}
		else
		{
			LOG_DEBUG("MQTT 服务已成功启动");
		}

		// 尝试启动心跳服务 (失败不退出)
		if (!plane::manager::Heartbeat::getInstance().start())
		{
			LOG_ERROR("错误: 心跳服务启动失败 (程序继续运行)");
		}
		else
		{
			LOG_DEBUG("心跳服务已成功启动");
			plane::manager::CatalogManager::getInstance().notifyHeartbeatRunning(true);
		}

		// 尝试初始化业务逻辑处理器 (失败不退出)
		if (!plane::manager::LogicHandler::getInstance().init())
		{
			LOG_ERROR("错误: 业务逻辑处理器初始化失败 (程序继续运行)");
		}
		else
		{
			LOG_DEBUG("业务逻辑处理器已成功初始化");
		}

		// 尝试启动遥测上报服务 (失败不退出)
		if (!plane::manager::TelemetryReporter::getInstance().start())
		{
			LOG_ERROR("错误: 遥测上报服务启动失败 (程序继续运行)");
		}
		else
		{
			LOG_DEBUG("遥测上报服务已成功启动");
			plane::manager::CatalogManager::getInstance().notifyTelemetryRunning(true);
		}

		// 尝试启动终端状态板 (可通过 enable_status_board 关闭; 失败不退出)
		if (config.isStatusBoardEnabled())
		{
			if (!plane::manager::StatusBoardManager::getInstance().start())
			{
				LOG_WARN("状态板服务启动失败 (程序继续运行)");
			}
			else
			{
				LOG_DEBUG("状态板服务已成功启动");
			}
		}
		else
		{
			plane::utils::StatusBoard::getInstance().setEnabled(false);
			LOG_INFO("终端状态板已按配置关闭");
		}

		// 等待一段时间让各服务稳定运行，随后报告应用已启动
		LOG_DEBUG("等待各服务稳定运行");
		_STD this_thread::sleep_for(_STD_CHRONO seconds(2));
		LOG_INFO("==========================================================");
		LOG_INFO("               应用程序初始化完成, 正在运行中");
		LOG_INFO("                    按 Ctrl+C 退出");
		LOG_INFO("==========================================================");

		// 注册信号处理
		_CSTD signal(SIGINT, _UNNAMED signalHandler);
		_CSTD signal(SIGTERM, _UNNAMED signalHandler);

		// 主循环，等待退出信号
		while (!g_should_exit)
		{
			_STD this_thread::sleep_for(_STD_CHRONO milliseconds(500));
		}

		// 收到退出信号，开始关闭各服务
		LOG_INFO("收到退出信号, 正在关闭应用程序");

		// 关闭各服务
		plane::manager::TelemetryReporter::getInstance().stop();
		plane::manager::Heartbeat::getInstance().stop();
		plane::manager::MQTTv5Service::getInstance().stop();

		// 如果启用标准 PSDK 作业流程，则停止 PSDKAdapter 和 PSDKManager
		if (config.isStandardProceduresEnabled())
		{
			plane::manager::PSDKManager::getInstance().stop();
			plane::manager::PSDKAdapter::getInstance().stop();

			if (PSDK_application_ptr_)
			{
				PSDK_application_ptr_.reset();
				LOG_DEBUG("PSDK CORE 已成功关闭");
			}
		}

		// 停止 WebSocket 订阅 (先于目录/绑定, 避免访问已停的目录)
		plane::manager::WsClient::getInstance().stop();

		// 停止设备绑定 (先于目录, 避免绑定流程访问已停的目录)
		plane::manager::DeviceBinder::getInstance().stop();

		// 停止 SwarmCatalog 目录客户端 (最后停止, 让退出前状态尽量上报)
		plane::manager::CatalogManager::getInstance().notifyPsdkRunning(false);
		plane::manager::CatalogManager::getInstance().notifyHeartbeatRunning(false);
		plane::manager::CatalogManager::getInstance().notifyTelemetryRunning(false);
		plane::manager::CatalogManager::getInstance().stop();

		// 等待一段时间确保所有服务已正确关闭
		_STD this_thread::sleep_for(_STD_CHRONO seconds(1));
		// 最后停止状态板同步: 退出过程中的状态变化 (连接断开等) 也真实反映到板面
		plane::manager::StatusBoardManager::getInstance().stop();
		LOG_INFO("应用程序已关闭");
		plane::utils::StatusBoard::getInstance().finish();
		return 0;
	}
} // namespace plane::my_dji

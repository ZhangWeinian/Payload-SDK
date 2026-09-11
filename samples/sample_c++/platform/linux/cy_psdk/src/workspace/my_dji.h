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
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <unistd.h>

#include <execinfo.h>

#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <fcntl.h>
#include <ucontext.h>

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

		// 崩溃转储 (Crash Dump)
		// 目标: 崩溃/异常终止时保留足够现场供离线分析:
		//   1) stderr 摘要 + 文本报告 (dumps/crash_<ts>_<pid>.txt: 信号/寄存器/调用栈/maps);
		//   2) 恢复默认信号处理并重发信号, 让内核按 core_pattern 生成完整 ELF core 转储 (dumps/core.*)。
		// 约束: 信号上下文只允许低层 async-signal-safe 调用 (open/write/read/close/backtrace*),
		//       不得使用 spdlog 等会加锁或分配内存的设施。
		char g_crashDumpDir[512] {};

		// 崩溃转储目录 (启动时准备一次; 失败则崩溃时仅输出 stderr 摘要)
		void setupCrashDumpDir(void) noexcept
		{
			try
			{
				const auto		dumpsPath { plane::utils::getEXEHomePath("dumps") };
				_STD error_code ec {};
				_STD_FS			create_directories(dumpsPath, ec);
				const auto		dumpsStr { dumpsPath.string() };
				if (!dumpsStr.empty() && dumpsStr.size() < sizeof(g_crashDumpDir))
				{
					_CSTD snprintf(g_crashDumpDir, sizeof(g_crashDumpDir), "%s", dumpsStr.c_str());
				}
			}
			catch (...)
			{
				// 忽略: 崩溃报告文件不可用时, 仍有 stderr 调用栈与内核 core 兜底
			}
		}

		// 放开 core 大小限制并保持进程可转储 (root / sudo 提权场景均尽量生效)
		void enableCoreDumps(void) noexcept
		{
			const struct rlimit coreLimit { RLIM_INFINITY, RLIM_INFINITY };
			(void)_CSTD			setrlimit(RLIMIT_CORE, &coreLimit);
			(void)_CSTD			prctl(PR_SET_DUMPABLE, 1);
		}

		// 信号编号 -> 名称 (避免在信号上下文调用非 async-signal-safe 的 strsignal)
		const char* signalName(int signum) noexcept
		{
			switch (signum)
			{
				case SIGSEGV:
					return "SIGSEGV";
				case SIGABRT:
					return "SIGABRT";
				case SIGBUS:
					return "SIGBUS";
				case SIGFPE:
					return "SIGFPE";
				case SIGILL:
					return "SIGILL";
				default:
					return "UNKNOWN";
			}
		}

		// 拷贝 /proc 下文件内容到 fd (信号上下文内仅用低层调用)
		void copyProcFileTo(int fd, const char* procPath) noexcept
		{
			const int srcFd { _CSTD open(procPath, O_RDONLY) };
			if (srcFd < 0)
			{
				return;
			}
			char	buf[4096] {};
			ssize_t readLen { 0 };
			while ((readLen = _CSTD read(srcFd, buf, sizeof(buf))) > 0)
			{
				(void)_CSTD write(fd, buf, static_cast<_STD size_t>(readLen));
			}
			_CSTD close(srcFd);
		}

		// 输出崩溃现场寄存器 (按目标架构取 ucontext 中的核心字段; 其余信息由 core 转储提供)
		void writeRegisterSnapshot(int fd, const void* context) noexcept
		{
			if (context == nullptr)
			{
				return;
			}
			char buf[256] {};
			int	 len { 0 };
#if defined(__aarch64__)
			const auto* uc { static_cast<const ucontext_t*>(context) };
			len = _CSTD snprintf(
				buf,
				sizeof(buf),
				"[registers] pc=0x%016llx sp=0x%016llx lr=0x%016llx fp=0x%016llx\n",
				static_cast<unsigned long long>(uc->uc_mcontext.pc),
				static_cast<unsigned long long>(uc->uc_mcontext.sp),
				static_cast<unsigned long long>(uc->uc_mcontext.regs[30]),
				static_cast<unsigned long long>(uc->uc_mcontext.regs[29])
			);
#elif defined(__x86_64__)
			const auto* uc { static_cast<const ucontext_t*>(context) };
			len = _CSTD snprintf(
				buf,
				sizeof(buf),
				"[registers] rip=0x%016llx rsp=0x%016llx rbp=0x%016llx\n",
				static_cast<unsigned long long>(uc->uc_mcontext.gregs[REG_RIP]),
				static_cast<unsigned long long>(uc->uc_mcontext.gregs[REG_RSP]),
				static_cast<unsigned long long>(uc->uc_mcontext.gregs[REG_RBP])
			);
#else
			// 其他架构: 寄存器快照略过 (core 转储中仍完整保留)
#endif
			if (len > 0)
			{
				const _STD size_t maxLen { sizeof(buf) - 1 };
				(void)_CSTD		  write(fd, buf, static_cast<_STD size_t>(len) < maxLen ? static_cast<_STD size_t>(len) : maxLen);
			}
		}

		// 写崩溃报告文件 dumps/crash_<ts>_<pid>.txt
		void writeCrashReportFile(int signum, siginfo_t* info, const void* context) noexcept
		{
			if (g_crashDumpDir[0] == '\0')
			{
				return;
			}

			char path[640] {};
			int	 pathLen { _CSTD snprintf(
				path,
				sizeof(path),
				"%s/crash_%lld_%d.txt",
				g_crashDumpDir,
				static_cast<long long>(_CSTD time(nullptr)),
				static_cast<int>(_CSTD getpid())
			) };
			if (pathLen <= 0)
			{
				return;
			}

			const int fd { _CSTD open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644) };
			if (fd < 0)
			{
				return;
			}

			char head[512] {};
			int	 headLen { _CSTD snprintf(
				head,
				sizeof(head),
				"========== [CRASH REPORT] ==========\n"
				"signal : %d (%s)\n"
				"si_code: %d\n"
				"addr   : %p\n"
				"pid    : %d\n"
				"epoch  : %lld\n"
				"build  : %s %s\n"
				"hint   : 内核 core 转储(若已生成)位于本目录 core.*; 可用 tools/crash_report.py 解析\n"
				"====================================\n",
				signum,
				signalName(signum),
				(info != nullptr) ? info->si_code : 0,
				(info != nullptr) ? info->si_addr : nullptr,
				static_cast<int>(_CSTD getpid()),
				static_cast<long long>(_CSTD time(nullptr)),
				__DATE__,
				__TIME__
			) };
			if (headLen > 0)
			{
				const _STD size_t maxLen { sizeof(head) - 1 };
				(void)_CSTD		  write(fd, head, static_cast<_STD size_t>(headLen) < maxLen ? static_cast<_STD size_t>(headLen) : maxLen);
			}

			writeRegisterSnapshot(fd, context);

			// 调用栈 (原始帧地址; 离线可用 addr2line/gdb 符号化)
			constexpr static char kStackTag[] { "[backtrace]\n" };
			(void)_CSTD			  write(fd, kStackTag, sizeof(kStackTag) - 1);
			void*				  frames[64] {};
			const int			  frameCount { _CSTD backtrace(frames, 64) };
			_CSTD				  backtrace_symbols_fd(frames, frameCount, fd);

			// 内存映射与进程状态 (离线解析地址归属/线程数所需)
			constexpr static char kMapsTag[] { "\n[memory maps]\n" };
			(void)_CSTD			  write(fd, kMapsTag, sizeof(kMapsTag) - 1);
			copyProcFileTo(fd, "/proc/self/maps");
			constexpr static char kStatusTag[] { "\n[proc status]\n" };
			(void)_CSTD			  write(fd, kStatusTag, sizeof(kStatusTag) - 1);
			copyProcFileTo(fd, "/proc/self/status");

			_CSTD close(fd);

			// stderr 提示报告路径
			char hint[768] {};
			int	 hintLen { _CSTD snprintf(hint, sizeof(hint), "!!! [CRASH] 详细报告已写入: %s !!!\n", path) };
			if (hintLen > 0)
			{
				const _STD size_t maxLen { sizeof(hint) - 1 };
				(void)_CSTD write(STDERR_FILENO, hint, static_cast<_STD size_t>(hintLen) < maxLen ? static_cast<_STD size_t>(hintLen) : maxLen);
			}
		}

		// 崩溃信号处理器:
		//  1) stderr 输出摘要与调用栈 (板上现场可直接看到);
		//  2) 写 dumps/crash_<ts>_<pid>.txt 详细报告;
		//  3) 恢复默认信号处理并重发信号, 让内核按 core_pattern 生成完整 core 转储。
		void crashSignalHandler(int signum, siginfo_t* info, void* context)
		{
			// 防重入: 多线程同时崩溃时只记录一次
			static volatile _CSTD sig_atomic_t entered { 0 };
			if (entered == 0)
			{
				entered = 1;

				char header[256] {};
				int	 len { _CSTD snprintf(
					header,
					sizeof(header),
					"\n!!! [CRASH] 信号 %d, 故障地址 %p, 调用栈如下 (请连同日志一并发给开发者) !!!\n",
					signum,
					(info != nullptr) ? info->si_addr : nullptr
				) };
				if (len > 0)
				{
					const _STD size_t maxLen { sizeof(header) - 1 };
					const _STD size_t writeLen { static_cast<_STD size_t>(len) < maxLen ? static_cast<_STD size_t>(len) : maxLen };
					(void)_CSTD		  write(STDERR_FILENO, header, writeLen);
				}

				void*				  frames[64] {};
				const int			  frameCount { _CSTD backtrace(frames, 64) };
				_CSTD				  backtrace_symbols_fd(frames, frameCount, STDERR_FILENO);
				constexpr static char kEndMsg[] { "!!! [CRASH] 调用栈结束 !!!\n" };
				(void)_CSTD			  write(STDERR_FILENO, kEndMsg, sizeof(kEndMsg) - 1);

				writeCrashReportFile(signum, info, context);

				// 生成内核 core 转储: 恢复默认处理, 解除信号屏蔽后重发信号,
				// 让内核按默认动作写出 core 文件并终止进程。
				// (注意: 处理器内本信号被自动屏蔽, 若只 raise 不解除屏蔽, 信号会滞留
				//  到处理器返回后才送达; 提前解除屏蔽可让内核立即执行默认动作。)
				_CSTD		   signal(signum, SIG_DFL);
				_CSTD sigset_t unblockSet {};
				_CSTD		   sigemptyset(&unblockSet);
				_CSTD		   sigaddset(&unblockSet, signum);
				(void)_CSTD	   sigprocmask(SIG_UNBLOCK, &unblockSet, nullptr);
				_CSTD		   raise(signum);
			}

			// 兜底 (正常流程下不可达): 重发信号后进程应已被内核终止
			_CSTD _Exit(128 + signum);
		}

		void installCrashDiagnostics(void) noexcept
		{
			enableCoreDumps();
			setupCrashDumpDir();

			struct sigaction action {};
			action.sa_sigaction = crashSignalHandler;
			action.sa_flags		= SA_SIGINFO | SA_RESETHAND;
			_CSTD sigemptyset(&action.sa_mask);
			_CSTD sigaction(SIGSEGV, &action, nullptr);
			_CSTD sigaction(SIGABRT, &action, nullptr);
			_CSTD sigaction(SIGBUS, &action, nullptr);
			_CSTD sigaction(SIGFPE, &action, nullptr);
			_CSTD sigaction(SIGILL, &action, nullptr);
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
		installCrashDiagnostics();

		LOG_INFO("==========================================================");
		LOG_INFO("                        应用程序启动中");
		LOG_INFO("==========================================================");

		// 崩溃转储目录 (installCrashDiagnostics 中准备): 崩溃报告与内核 core 均落于此
		if (g_crashDumpDir[0] != '\0')
		{
			LOG_INFO("崩溃转储目录: {}", g_crashDumpDir);
		}

		// 崩溃转储链路自测: CY_PSDK_CRASH_TEST=1 时主动触发一次崩溃 (用于验证报告与 core 生成)
		if (const char* crashTest { _CSTD getenv("CY_PSDK_CRASH_TEST") }; crashTest != nullptr && crashTest[0] != '\0')
		{
			LOG_WARN("CY_PSDK_CRASH_TEST 已设置: 主动触发 SIGSEGV 验证崩溃转储链路");
			_CSTD raise(SIGSEGV);
		}

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

		// 终端状态板: 尽早拉起 (先于各服务), 覆盖整个启动过程 (含耗时的 PSDK 初始化);
		// 后续任一环节 fail-fast 退出时, 板面也能反映退出前的状态, 便于定位问题
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
				LOG_INFO("PSDK CORE 初始化完成");
				_STD this_thread::sleep_for(_STD_CHRONO seconds(5));
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

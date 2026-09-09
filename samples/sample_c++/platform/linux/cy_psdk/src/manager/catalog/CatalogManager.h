// cy_psdk/manager/catalog/CatalogManager.h
//
// SwarmCatalog 服务目录客户端接入 (可选, 非阻塞附属能力)。
//
// 职责:
//   - 后台线程管理 CatalogRuntime 生命周期: 发现 -> 注册 -> 心跳(库内部) -> 周期状态上报
//   - 目录 READY 后, 若配置启用 discover_broker, 解析"中心"服务的 mqtt 端点并切换动态 MQTT broker
//   - 失败语义: 无 /etc/catalog.yml、目录不可达等均只告警降级, 不影响 PSDK/MQTT 主链路
//
// 线程模型:
//   start() 仅派发后台线程立即返回; 后台线程内部按库的线程规则使用 CatalogRuntime
//   (事件回调在库回调线程串行执行, 回调内只置标志/记日志, 不做阻塞 IO)。
//
// 编译期开关: 未启用 ENABLE_CATALOG_CLIENT 时(CATALOG_ENABLED 未定义)本类为 no-op。

#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include "define.h"

namespace plane::manager
{
	class CatalogManager
	{
	public:
		static CatalogManager& getInstance(void) noexcept;

		// 后台启动目录客户端 (发现->注册->周期状态上报)。配置未启用/降级失败时不阻塞调用方。
		void start(void) noexcept;

		// 停止目录客户端并 join 后台线程
		void stop(void) noexcept;

		// 供启动链注入的组件状态 (状态上报组件: psdk / heartbeat / telemetry)
		void notifyPsdkRunning(bool running) noexcept;
		void notifyHeartbeatRunning(bool running) noexcept;
		void notifyTelemetryRunning(bool running) noexcept;

	private:
		CatalogManager(void) noexcept;
		~CatalogManager(void) noexcept;
		CatalogManager(const CatalogManager&)			 = delete;
		CatalogManager& operator=(const CatalogManager&) = delete;

		// 实现体(定义于 .cpp, 持有 CatalogRuntime 等目录客户端对象)
		struct Impl;
		_STD unique_ptr<Impl> impl_ {};

		void				  runLoop(void) noexcept;

		// 周期状态上报 (仅后台线程调用)
		void reportStatus(void) noexcept;

		// Ready 后解析中心服务 mqtt 端点并切换动态 broker, 返回是否已处理 (仅后台线程调用)
		bool trySwitchMqttBroker(void) noexcept;

		// 后台线程生命周期 (防止 start()/stop() 重入)
		_STD atomic<bool> started_ { false };
		_STD atomic<bool> running_ { false };
		_STD thread		  thread_ {};

		// 组件状态 (状态上报用; 由启动链在相应服务就绪/停止时更新)
		_STD atomic<bool> psdk_running_ { false };
		_STD atomic<bool> heartbeat_running_ { false };
		_STD atomic<bool> telemetry_running_ { false };

		// 目录是否已注册成功 (Ready / RegistrationSucceeded)
		_STD atomic<bool> catalog_ready_ { false };
	};
} // namespace plane::manager

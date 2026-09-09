// cy_psdk/manager/catalog/CatalogManager.h
//
// SwarmCatalog 客户端接入 (自研 plane::catalog 运行时)。
//
// 职责:
//   - 后台线程管理 CatalogRuntime 生命周期: 发现 -> 注册 -> 心跳/状态(运行时内部)
//   - 目录 READY 后解析"中心"mqtt 服务的端点并切换动态 MQTT broker
//   - 业务壳周期(3s)上报组件健康状态
//
// 线程模型:
//   start() 仅派发后台线程立即返回; CatalogRuntime 内部有控制线程与回调线程
//   (回调串行执行, 回调内只置标志/记日志, 不做阻塞 IO)。

#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "define.h"

namespace plane::manager
{
	class CatalogManager
	{
	public:
		static CatalogManager& getInstance(void) noexcept;

		// 后台启动目录客户端 (发现->注册->周期状态上报)。发现参数缺失/降级失败时不阻塞调用方。
		void start(void) noexcept;

		// 停止目录客户端并 join 后台线程
		void stop(void) noexcept;

		// 供启动链注入的组件状态 (状态上报组件: psdk / heartbeat / telemetry)
		void notifyPsdkRunning(bool running) noexcept;
		void notifyHeartbeatRunning(bool running) noexcept;
		void notifyTelemetryRunning(bool running) noexcept;

		// 目录是否已注册成功 (Ready)
		_NODISCARD bool isCatalogReady(void) const noexcept
		{
			return this->catalog_ready_.load(_STD memory_order_acquire);
		}

		// 解析已注册业务服务的基础地址 (如 "swarm.service.base"), 返回 "scheme://ip:port"; 失败返回空串
		_NODISCARD _STD string resolveServiceBaseUrl(const _STD string& service_id, const _STD string& protocol) noexcept;

		// 绑定/昵称联动: 更新目录注册的 service_name (触发运行时重注册); 空串恢复默认名
		void updateServiceName(const _STD string& service_name) noexcept;

	private:
		CatalogManager(void) noexcept;
		~CatalogManager(void) noexcept;
		CatalogManager(const CatalogManager&)			 = delete;
		CatalogManager& operator=(const CatalogManager&) = delete;

		struct Impl;
		_STD unique_ptr<Impl> impl_ {};

		// 后台线程入口: 发现/注册主循环
		void runLoop(void) noexcept;

		// 周期状态上报 (仅后台线程调用; 固定 3s)
		void reportStatus(void) noexcept;

		// Ready 后解析中心服务 mqtt 端点并切换动态 broker (仅后台线程调用)
		bool trySwitchMqttBroker(void) noexcept;

		// 后台线程生命周期 (防止 start()/stop() 重入)
		_STD atomic<bool> started_ { false };
		_STD atomic<bool> running_ { false };
		_STD thread		  thread_ {};

		// 保护 impl_/运行时 跨线程访问 (resolveServiceBaseUrl/updateServiceName 可被业务线程调用)
		mutable _STD mutex rt_mutex_ {};

		// 组件状态 (状态上报用; 由启动链在相应服务就绪/停止时更新)
		_STD atomic<bool> psdk_running_ { false };
		_STD atomic<bool> heartbeat_running_ { false };
		_STD atomic<bool> telemetry_running_ { false };

		// 目录是否已注册成功 (Ready / RegistrationSucceeded)
		_STD atomic<bool> catalog_ready_ { false };
	};
} // namespace plane::manager

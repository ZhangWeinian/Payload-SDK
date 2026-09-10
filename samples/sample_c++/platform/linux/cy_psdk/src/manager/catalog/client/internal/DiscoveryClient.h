// cy_psdk/manager/catalog/client/internal/DiscoveryClient.h
//
// UDP 发现客户端抽象 (对齐 java DiscoveryClient)。CatalogRuntime 通过它执行
// 周期探测; 平台可实现或注入自定义发现实现。

#pragma once

#include <atomic>

#include "define.h"
#include "manager/catalog/client/DiscoveryConfig.h"
#include "manager/catalog/client/internal/DiscoveryReport.h"

namespace plane::catalog::internal
{
	class DiscoveryClient
	{
	public:
		virtual ~DiscoveryClient(void) = default;

		// 同步探测; cancelled 置位时尽快返回
		virtual DiscoveryReport discover(const DiscoveryConfig& config, const _STD atomic<bool>& cancelled) = 0;

		// 注册成功后记录最近可用目录 IP
		virtual void saveSuccessfulIp(const _STD string& ip) = 0;
	};
} // namespace plane::catalog::internal

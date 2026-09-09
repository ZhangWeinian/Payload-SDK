// cy_psdk/manager/catalog/client/DiscoveryConfig.h
//
// 发现配置 (nodeId/port/targets/时间参数)。由业务侧构造并注入 CatalogRuntime,
// 不读取任何系统 yml (对齐 java DiscoveryConfig 的注入式用法)。

#pragma once

#include <chrono>
#include <string>
#include <vector>

#include "define.h"

namespace plane::catalog
{
	// UDP 目录发现配置
	struct DiscoveryConfig
	{
		_STD string node_id {};				 // 探测包携带的节点 ID (需与服务端 local-node-id 匹配才会回复)
		int			port { 0 };				 // UDP 探测端口 (服务端 swarm.udp.port, 默认 30906)
		_STD vector<_STD string> targets {}; // 探测目标 (单 IP / 末段通配 .* / CIDR / 起止范围)

		// 等待响应窗口
		_STD_CHRONO milliseconds response_window { 500 };
		// 初始重试间隔 (保留字段, 与 java 对齐)
		_STD_CHRONO milliseconds retry_initial { 1000 };
		// READY 后周期重发现的间隔
		_STD_CHRONO milliseconds ready_probe_interval { 30'000 };
	};
} // namespace plane::catalog

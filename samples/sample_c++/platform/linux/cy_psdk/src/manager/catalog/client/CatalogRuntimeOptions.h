// cy_psdk/manager/catalog/client/CatalogRuntimeOptions.h
//
// 业务侧运行时设置。发现配置不在其中 (由 DiscoveryConfig 单独提供, 对齐 java)。

#pragma once

#include <chrono>
#include <functional>

#include "define.h"
#include "manager/catalog/client/CatalogModels.h"
#include "manager/catalog/client/CatalogTypes.h"

namespace plane::catalog
{
	// 业务面向的运行时设置 (对齐 java CatalogRuntimeOptions)
	struct CatalogRuntimeOptions
	{
		ServiceRegistration registration {};

		// 连接与总请求超时, 非正数使用默认 10 秒
		_STD_CHRONO milliseconds http_timeout { 10'000 };

		// 心跳间隔, 非正数使用默认 3 秒
		_STD_CHRONO milliseconds heartbeat_interval { 3000 };

		// 配置轮询检查间隔, 非正数使用默认 5 秒
		_STD_CHRONO milliseconds config_check_interval { 5000 };

		// 连续失败达到该阈值进入 UNAVAILABLE (对齐 java unavailableFailureThreshold, 默认 3)
		int unavailable_failure_threshold { 3 };

		// 运行时事件回调。在专用回调线程串行执行; 空则不投递。
		_STD function<void(const CatalogEvent&)> event_callback {};
	};
} // namespace plane::catalog

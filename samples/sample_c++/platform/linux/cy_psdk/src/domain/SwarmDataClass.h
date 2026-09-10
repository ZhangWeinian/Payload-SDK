// cy_psdk/domain/SwarmDataClass.h

#pragma once

#include <cstdint>
#include <string>

#include "define.h"

namespace plane::domain
{
	// Swarm 订阅者
	struct SwarmSubscriber
	{
		int64_t		timestamp { 0 };		// 最近心跳时间戳
		_STD string subscriber_ip { "" };	// 订阅者 IP
		_STD string subscriber_name { "" }; // 订阅者名称
		bool		selected { true };		// 是否选中 (新订阅者默认选中)
	};
} // namespace plane::domain

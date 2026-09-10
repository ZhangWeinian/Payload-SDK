// cy_psdk/domain/CatalogNode.h

#pragma once

#include <string>
#include <vector>

#include "define.h"

namespace plane::domain
{
	// Catalog 候选节点来源: 手动指定 / UDP 自动发现
	enum class CatalogNodeSource
	{
		MANUAL = 0, // 用户手动指定 (持久化)
		DISCOVERED	// UDP 广播自动发现 (运行时)
	};

	struct CatalogNode
	{
		_STD string nodeId { "" };			 // Catalog 节点 ID
		_STD vector<_STD string> targets {}; // 探测目标 (IP/网段)
		CatalogNodeSource		 source { CatalogNodeSource::MANUAL };
	};
} // namespace plane::domain

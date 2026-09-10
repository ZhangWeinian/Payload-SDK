// cy_psdk/manager/catalog/client/internal/discovery/DiscoveryReport.h
//
// 探测结果状态与一轮探测报告 (对齐 java DiscoveryStatus/DiscoveryReport)。

#pragma once

#include <string>
#include <vector>

#include "define.h"
#include "manager/catalog/client/CatalogTypes.h"

namespace plane::catalog::internal
{
	// 探测结果状态 (对齐 java DiscoveryStatus)
	enum class DiscoveryStatus
	{
		OK = 0,
		NOT_FOUND,
		INVALID_ARGUMENT,
		SOCKET_ERROR
	};

	// 一轮探测报告 (对齐 java DiscoveryReport)
	struct DiscoveryReport
	{
		DiscoveryStatus status { DiscoveryStatus::NOT_FOUND };
		_STD string		error {};
		_STD vector<CatalogEndpoint> endpoints {};
		bool						 multiple_instances { false };
	};
} // namespace plane::catalog::internal

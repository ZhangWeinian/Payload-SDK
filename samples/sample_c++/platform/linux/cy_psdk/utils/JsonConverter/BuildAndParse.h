// cy_psdk/utils/JsonConverter/BuildAndParse.h

#pragma once

#include "protocol/DroneDataClass.h"
#include "protocol/HeartbeatDataClass.h"

#include <string_view>
#include <string>
#include <vector>

#include "define.h"

namespace plane::utils
{
	class JsonConverter
	{
	public:
		// 构建一个 JSON 字符串，包含所有状态信息
		_NODISCARD static _STD string buildStatusReportJson(const plane::protocol::StatusPayload& payload) noexcept;

		// 构建一个 JSON 字符串，包含任务信息
		_NODISCARD static _STD string buildMissionInfoJson(const plane::protocol::MissionInfoPayload& payload) noexcept;

		// 构建一个 JSON 字符串，包含健康状态信息
		_NODISCARD static _STD string buildHealthStatusJson(const plane::protocol::HealthStatusPayload& payload) noexcept;

		// 构建一个 JSON 字符串，包含任务进度信息
		_NODISCARD static _STD string buildMissionProgressJson(const plane::protocol::MissionProgressPayload& payload) noexcept;

		// 解析 JSON 字符串并路由到相应的处理函数
		static void parseAndRouteMessage(_STD string_view topic, _STD string_view jsonString) noexcept;
	};
} // namespace plane::utils

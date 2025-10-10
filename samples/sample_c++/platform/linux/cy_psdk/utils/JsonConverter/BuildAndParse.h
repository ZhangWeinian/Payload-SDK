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
		_NODISCARD static _STD string buildStatusReportJson(const plane::protocol::StatusPayload& payload) noexcept;
		_NODISCARD static _STD string buildMissionInfoJson(const plane::protocol::MissionInfoPayload& payload) noexcept;
		_NODISCARD static _STD string buildHealthStatusJson(const plane::protocol::HealthStatusPayload& payload) noexcept;
		_NODISCARD static _STD string buildMissionProgressJson(const plane::protocol::MissionProgressPayload& payload) noexcept;

		static void					  parseAndRouteMessage(_STD string_view topic, _STD string_view jsonString) noexcept;
	};
} // namespace plane::utils

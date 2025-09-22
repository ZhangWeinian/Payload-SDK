#pragma once

#include "define.h"
#include "protocol/DroneDataClass.h"

#include <string_view>
#include <string>
#include <vector>

namespace plane::utils
{
	class JsonConverter
	{
	public:
		_NODISCARD static _STD string buildStatusReportJson(const protocol::StatusPayload& payload) noexcept;
		_NODISCARD static _STD string buildMissionInfoJson(const protocol::MissionInfoPayload& payload) noexcept;
		_NODISCARD static _STD string buildHealthStatusJson(const _STD vector<protocol::HealthAlertPayload>& alerts) noexcept;

		static void					  parseAndRouteMessage(_STD string_view topic, _STD string_view jsonString) noexcept;
	};
} // namespace plane::utils

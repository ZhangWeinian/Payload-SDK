#pragma once

#include "protocol/DroneDataClass.h"

#include <string>
#include <vector>

namespace plane::utils
{
#ifndef _NODISCARD
	#define _NODISCARD [[nodiscard]]
#endif

	class JsonConverter
	{
	public:
		_NODISCARD static std::string buildStatusReportJson(const protocol::StatusPayload& payload) noexcept;
		_NODISCARD static std::string buildMissionInfoJson(const protocol::MissionInfoPayload& payload) noexcept;
		_NODISCARD static std::string buildHealthStatusJson(const std::vector<protocol::HealthAlertPayload>& alerts) noexcept;

		static void					  parseAndRouteMessage(const std::string& topic, const std::string& jsonString) noexcept;
	};
} // namespace plane::utils

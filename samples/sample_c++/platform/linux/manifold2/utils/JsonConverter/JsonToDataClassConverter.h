#pragma once

#include "protocol/JsonProtocol.h"

#include <string>
#include <vector>

namespace plane::utils
{
	class JsonConverter
	{
	public:
		static std::string buildStatusReportJson(const protocol::StatusPayload& payload) noexcept;
		static std::string buildMissionInfoJson(const protocol::MissionInfoPayload& payload) noexcept;
		static std::string buildHealthStatusJson(const std::vector<protocol::HealthAlertPayload>& alerts) noexcept;

		static void		   parseAndRouteMessage(const std::string& topic, const std::string& jsonString) noexcept;
	};
} // namespace plane::utils

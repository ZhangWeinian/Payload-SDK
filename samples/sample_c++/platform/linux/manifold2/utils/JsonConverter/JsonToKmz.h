#pragma once

#include "protocol/DroneDataClass.h"

#include <string>
#include <vector>

namespace plane::utils
{
	class JsonToKmzConverter
	{
	public:
		static bool		   convertWaypointsToKmz(const std::vector<protocol::Waypoint>& waypoints,
												 const protocol::WaypointPayload&		missionInfo = {}) noexcept;

		static std::string getKmzFilePath(void) noexcept;
	};
} // namespace plane::utils

#pragma once

#include "protocol/DroneDataClass.h"

#include <string>
#include <vector>

namespace plane::utils
{
#ifndef _NODISCARD
	#define _NODISCARD [[nodiscard]]
#endif

	class JsonToKmzConverter
	{
	public:
		_NODISCARD static bool		  convertWaypointsToKmz(const std::vector<protocol::Waypoint>& waypoints,
															const protocol::WaypointPayload&	   missionInfo = {}) noexcept;

		_NODISCARD static std::string getKmzFilePath(void) noexcept;
	};
} // namespace plane::utils

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
		_NODISCARD static bool			   convertWaypointsToKmz(const std::vector<plane::protocol::Waypoint>& waypoints,
																 const plane::protocol::WaypointPayload&	   missionInfo = {}) noexcept;

		_NODISCARD static std::string_view getKmzFilePath(void) noexcept;
	};
} // namespace plane::utils

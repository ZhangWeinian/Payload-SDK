// manifold2/utils/JsonConverter/JsonToKmz.h

#pragma once

#include "define.h"
#include "protocol/DroneDataClass.h"

#include <string>
#include <vector>

namespace plane::utils
{
	class JsonToKmzConverter
	{
	public:
		_NODISCARD static bool		  convertWaypointsToKmz(const _STD vector<plane::protocol::Waypoint>& waypoints,
															const plane::protocol::WaypointPayload&		  missionInfo = {}) noexcept;

		_NODISCARD static _STD string getKmzFilePath(void) noexcept;
	};
} // namespace plane::utils

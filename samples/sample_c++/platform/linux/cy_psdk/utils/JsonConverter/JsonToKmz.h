// cy_psdk/utils/JsonConverter/JsonToKmz.h

#pragma once

#include "protocol/DroneDataClass.h"

#include <string>
#include <vector>

#include "define.h"

namespace plane::utils
{
	class JsonToKmzConverter
	{
	public:
		_NODISCARD static _STD		  optional<_DEFINED _KMZ_DATA_TYPE>
									  convertWaypointsToKmz(const _STD vector<plane::protocol::Waypoint>& waypoints,
															const plane::protocol::WaypointPayload&		  missionInfo = {}) noexcept;

		_NODISCARD static _STD string getKmzFilePath(void) noexcept;
	};
} // namespace plane::utils

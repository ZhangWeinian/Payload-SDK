#pragma once

#include "protocol/JsonProtocol.h"

#include <string>
#include <vector>

namespace plane::utils
{
	class JsonToKmzConverter
	{
	public:
		bool convertWaypointsToKmz(const std::vector<protocol::Waypoint>& waypoints,
								   const std::string&					  kmzFilePath,
								   const protocol::WaypointPayload&		  missionInfo = {});

	private:
		std::string generateWaylinesKml(const std::vector<protocol::Waypoint>& waypoints);
		std::string generateTemplateKml();

		double		calculateDistance(const protocol::Waypoint& wp1, const protocol::Waypoint& wp2);
		double		calculateTotalDistance(const std::vector<protocol::Waypoint>& waypoints);
		double		calculateTotalDuration(const std::vector<protocol::Waypoint>& waypoints);
		double		calculateHeadingAngle(const protocol::Waypoint& from, const protocol::Waypoint& to);
	};
} // namespace plane::utils

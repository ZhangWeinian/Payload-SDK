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
		// 将航点列表转换为 KMZ 格式的数据
		_NODISCARD static _STD optional<_DEFINED _KMZ_DATA_TYPE>
							   convertWaypointsToKmz(const _STD vector<plane::protocol::Waypoint>& waypoints,
													 const plane::protocol::WaypointPayload&	   missionInfo = {}) noexcept;

		// 获取当前（最新一次） KMZ 文件的存储路径
		_NODISCARD static _STD string getKmzFilePath(void) noexcept;
	};
} // namespace plane::utils

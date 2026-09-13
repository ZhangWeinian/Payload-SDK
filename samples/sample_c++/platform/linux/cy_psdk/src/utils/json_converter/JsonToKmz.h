// cy_psdk/utils/json_converter/JsonToKmz.h

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
        [[nodiscard]] static ::std::optional<kmz_data_type> convertWaypointsToKmz(
            const ::std::vector<plane::protocol::Waypoint>& waypoints,
            const plane::protocol::WaypointPayload&         missionInfo = {}
        ) noexcept;

        // 获取当前（最新一次） KMZ 文件的存储路径
        [[nodiscard]] static ::std::string getKmzFilePath(void) noexcept;
    };
} // namespace plane::utils

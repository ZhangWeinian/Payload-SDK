#pragma once

#include "protocol/JsonProtocol.h"

#include <string>
#include <vector>

namespace Json
{
	class Value;
} // namespace Json

namespace plane::utils
{
	/**
     * @class JsonConverter
     * @brief 一个静态工具类，负责将 C++ 数据结构与项目定义的 JSON 格式进行相互转换。
     */
	class JsonConverter
	{
	public:
		// 将 C++ 结构体转换为 JSON 字符串
		static std::string buildStatusReportJson(const protocol::StatusPayload& payload) noexcept;
		static std::string buildMissionInfoJson(const protocol::MissionInfoPayload& payload) noexcept;
		static std::string buildHealthStatusJson(const std::vector<protocol::HealthAlertPayload>& alerts) noexcept;
	};
} // namespace plane::utils

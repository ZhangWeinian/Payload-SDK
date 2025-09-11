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
		static std::string buildStatusReportJson(const protocol::StatusPayload& payload);
		static std::string buildMissionInfoJson(const protocol::MissionInfoPayload& payload);
		static std::string buildHealthStatusJson(const std::vector<protocol::HealthAlertPayload>& alerts);

		// 解析函数: 将 JSON 字符串解析为 C++ 结构体并分发
		static void parseAndRouteMessage(const std::string& topic, const std::string& jsonString);
	};
} // namespace plane::utils

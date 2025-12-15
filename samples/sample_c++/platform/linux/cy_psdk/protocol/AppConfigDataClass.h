// cy_psdk/protocol/AppConfigDataClass.h

#pragma once

#include <nlohmann/json.hpp>

#include <string_view>
#include <cstdint>
#include <optional>
#include <string>

#include "define.h"

namespace plane::protocol
{
	using n_json = _NLOHMANN_JSON json;

	struct AppConfigData
	{
		_STD string_view mqttUrl {};					// MQTT 服务器地址
		_STD string_view planeSn {};					// 飞机序列号（自动识别）
		_STD string_view planeCode {};					// 飞机识别码（手动定义）
		_STD string		 mqttClientId {};				// MQTT 客户端 ID（自动生成）
		bool			 enableFullPSDK { false };		// 是否启用完整 PSDK 功能
		bool			 enableTraceLogLevel { false }; // 是否启用跟踪日志级别
		_STD uint8_t	 psdkLogLevel { 3 };			// 设置 PSDK 日志级别
		bool			 enableSkipRC { false };		// 是否启用跳过遥控器
		bool			 enableSaveKmzFile { false };	// 是否启用保存 KMZ 文件
		bool			 enableUseTestKmz { false };	// 是否启用测试 KMZ 文件
		_STD string_view testKmzFilePath {};			// 测试 KMZ 文件路径，仅在启用测试 KMZ 文件时有效
	};
} // namespace plane::protocol

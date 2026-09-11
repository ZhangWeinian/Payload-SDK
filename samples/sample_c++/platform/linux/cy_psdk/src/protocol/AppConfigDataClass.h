// cy_psdk/protocol/AppConfigDataClass.h

#pragma once

#include <nlohmann/json.hpp>

#include <string_view>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "define.h"

namespace plane::protocol
{
	using n_json = _NLOHMANN_JSON json;

	// SwarmCatalog 服务目录发现配置 (node_id/port/targets, 由 config.yml catalog 小节提供)
	struct CatalogConfig
	{
		_STD string	  node_id { "" };		 // 探测身份 (需与服务端 local-node-id 匹配才会回复)
		_STD uint16_t port { 30'906 };		 // UDP 探测端口 (服务端 swarm.udp.port)
		_STD vector<_STD string> targets {}; // 探测目标 (单 IP / 末段通配 .* / CIDR / 起止范围)
	};

	struct AppConfigData
	{
		_STD string_view mqttUrl {};					// MQTT 服务器地址 (可选; 缺省由 SwarmCatalog 服务发现提供)
		_STD string_view planeSn {};					// 飞机序列号 (预留)
		_STD string_view planeCode {};					// 设备标识 (可选; 未配置时使用 PSDK 飞控序列号)
		_STD string		 mqttClientId {};				// MQTT 客户端 ID（自动生成）
		bool			 enableFullPSDK { false };		// 是否启用完整 PSDK 功能
		bool			 enableTraceLogLevel { false }; // 是否启用跟踪日志级别
		_STD uint8_t	 psdkLogLevel { 3 };			// 设置 PSDK 日志级别
		bool			 enableSkipRC { false };		// 是否启用跳过遥控器
		bool			 enableSaveKmzFile { false };	// 是否启用保存 KMZ 文件
		bool			 enableStatusBoard { true };	// 是否启用终端状态板

		// RID 起降点 (真实位置; 由部署配置提供, 单位: 度 / 米; 0=未配置)
		double		  takeoffLatitudeDeg { 0.0 };  // 纬度
		double		  takeoffLongitudeDeg { 0.0 }; // 经度
		double		  takeoffAltitudeM { 0.0 };	   // 海拔

		CatalogConfig catalog {};				   // SwarmCatalog 服务目录接入配置
	};
} // namespace plane::protocol

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

	// SwarmCatalog 服务目录接入配置
	struct CatalogConfig
	{
		bool		  enabled { false };				 // 是否启用 SwarmCatalog 接入
		_STD string	  serviceId {};						 // 注册 service_id (空 → 运行时按 payload-<planeCode> 生成)
		_STD string	  serviceName {};					 // 注册 service_name (空 → DJI 载荷代理-<planeCode>)
		_STD string	  version { "1.0.0" };				 // 注册版本号
		_STD uint32_t heartbeatIntervalMs { 3000 };		 // 目录心跳间隔
		_STD uint32_t statusReportIntervalMs { 10'000 }; // 状态快照上报间隔
		bool		  discoverBroker { false };			 // 是否用目录解析动态 MQTT broker
		_STD string	  brokerServiceId {};				 // 待解析的"中心"服务 service_id
		_STD string	  brokerPortProtocol { "mqtt" };	 // 取该服务的哪个端点协议
	};

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

		CatalogConfig	 catalog {};					// SwarmCatalog 服务目录接入配置
	};
} // namespace plane::protocol

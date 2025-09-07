#pragma once

#include "yaml-cpp/yaml.h"

#include <string>

class ConfigManager
{
public:
	// 获取单例实例
	static ConfigManager& getInstance();

	// 加载配置文件
	bool load(const std::string& filepath);

	// 获取配置项
	std::string getMqttUrl() const;
	std::string getMqttClientId() const;

private:
	ConfigManager()								   = default; // 私有构造函数
	~ConfigManager()							   = default;
	ConfigManager(const ConfigManager&)			   = delete;
	ConfigManager& operator=(const ConfigManager&) = delete;

	YAML::Node	   configNode; // 用于存储解析后的 YAML 数据
	bool		   loaded = false;
};

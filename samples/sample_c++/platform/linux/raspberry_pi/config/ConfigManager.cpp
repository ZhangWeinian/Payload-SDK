// raspberry_pi/cinfig/ConfigManager.cpp

#include "config/ConfigManager.h"
#include "utils/Logger.h"

#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>

namespace plane::config
{
	ConfigManager::ConfigManager(void) noexcept
	{
		this->app_config_.mqttClientId = this->generateUniqueClientId();
		LOG_DEBUG("运行时 MQTT Client ID 已生成: {}", this->app_config_.mqttClientId);
	}

	ConfigManager& ConfigManager::getInstance(void) noexcept
	{
		static ConfigManager instance {};
		return instance;
	}

	bool ConfigManager::loadAndCheck(const _STD string& filepath) noexcept
	{
		this->loaded_ = false;

		try
		{
			if (_STD ifstream file(filepath); !file.good())
			{
				LOG_ERROR("配置文件未找到: {}", filepath);
				return false;
			}

			this->config_node_ = _YAML LoadFile(filepath);
			LOG_DEBUG("成功加载配置文件: {}", filepath);

			if (!this->validateConfig())
			{
				LOG_ERROR("配置验证失败");
				return false;
			}

			this->loaded_ = true;
			return true;
		}
		catch (const _YAML Exception& e)
		{
			LOG_ERROR("解析 YAML 配置文件 '{}' 失败: {}", filepath, e.what());
			return false;
		}
		catch (const _STD exception& e)
		{
			LOG_ERROR("处理配置文件时发生未知异常: {}", e.what());
			return false;
		}
	}

	bool ConfigManager::validateConfig(void) noexcept
	{
		try
		{
			if (this->config_node_["mqtt"] && this->config_node_["mqtt"]["url"])
			{
				_STD string url { this->config_node_["mqtt"]["url"].as<_STD string>() };
				if (url.empty())
				{
					LOG_ERROR("MQTT URL 不能为空");
					return false;
				}
				this->app_config_.mqttUrl = url;
			}
			else
			{
				LOG_ERROR("配置文件中缺少 'mqtt.url'");
				return false;
			}

			if (this->config_node_["plane"] && this->config_node_["plane"]["code"])
			{
				_STD string code { this->config_node_["plane"]["code"].as<_STD string>() };
				if (code.empty())
				{
					LOG_ERROR("Plane Code 不能为空");
					return false;
				}
				this->app_config_.planeCode = code;
			}
			else
			{
				LOG_ERROR("配置文件中缺少 'plane.code'");
				return false;
			}

			return true;
		}
		catch (const _STD exception& e)
		{
			LOG_ERROR("验证配置时发生异常: {}", e.what());
			return false;
		}
	}

	_STD string ConfigManager::generateUniqueClientId(void) noexcept
	{
		_STD random_device rd {};
		_STD mt19937	   gen(rd());
		_STD uniform_int_distribution<> dis(0, 15);
		_STD uniform_int_distribution<> dis2(8, 11);

		_STD stringstream				ss {};
		ss << _STD						hex;

		for (int i { 0 }; i < 32; i++)
		{
			if (i == 12)
			{
				ss << 4;
			}
			else if (i == 16)
			{
				ss << dis2(gen);
			}
			else
			{
				ss << dis(gen);
			}
		}

		return "cv_" + ss.str();
	}

	_STD string ConfigManager::getMqttUrl(void) const noexcept
	{
		if (this->loaded_)
		{
			return this->app_config_.mqttUrl;
		}
		LOG_WARN("配置未加载或 MQTT URL 无效, 返回空字符串");
		return "";
	}

	_STD string ConfigManager::getMqttClientId(void) const noexcept
	{
		return this->app_config_.mqttClientId;
	}

	_STD string ConfigManager::getPlaneCode(void) const noexcept
	{
		if (this->loaded_)
		{
			return this->app_config_.planeCode;
		}
		LOG_WARN("配置未加载或 PlaneCode 无效, 返回空字符串");
		return "";
	}
} // namespace plane::config

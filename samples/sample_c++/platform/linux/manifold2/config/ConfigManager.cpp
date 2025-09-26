// manifold2/cinfig/ConfigManager.cpp

#include "utils/Logger.h"

#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>

#include "config/ConfigManager.h"

namespace plane::config
{
	ConfigManager& ConfigManager::getInstance(void) noexcept
	{
		static ConfigManager instance {};
		return instance;
	}

	bool ConfigManager::loadAndCheck(const _STD string& filepath) noexcept
	{
		configFilePath = filepath;
		loaded		   = false;

		try
		{
			if (_STD ifstream file(filepath); !file.good())
			{
				LOG_ERROR("配置文件未找到: {}", filepath);
				return false;
			}

			configNode = YAML::LoadFile(filepath);
			LOG_DEBUG("成功加载配置文件: {}", filepath);

			if (!validateAndFixConfig())
			{
				LOG_ERROR("配置验证失败");
				return false;
			}

			loaded = true;
			return true;
		}
		catch (const YAML::Exception& e)
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

	bool ConfigManager::validateAndFixConfig(void) noexcept
	{
		try
		{
			if (configNode["mqtt"] && configNode["mqtt"]["url"])
			{
				_STD string url { configNode["mqtt"]["url"].as<_STD string>() };
				if (url.empty())
				{
					LOG_ERROR("MQTT URL 不能为空");
					return false;
				}
				appConfig.mqttUrl = url;
			}
			else
			{
				LOG_ERROR("配置文件中缺少 MQTT URL");
				return false;
			}

			if (configNode["plane"] && configNode["plane"]["code"])
			{
				_STD string code { configNode["plane"]["code"].as<_STD string>() };
				if (code.empty())
				{
					LOG_ERROR("Plane Code 不能为空");
					return false;
				}
				appConfig.planeCode = code;
			}
			else
			{
				LOG_ERROR("配置文件中缺少 Plane Code");
				return false;
			}

			if (configNode["mqtt"] && configNode["mqtt"]["client_id"])
			{
				_STD string clientId { configNode["mqtt"]["client_id"].as<_STD string>() };
				if (clientId.empty())
				{
					clientId						= generateUuidWithoutDashes();
					appConfig.mqttClientId			= clientId;

					configNode["mqtt"]["client_id"] = clientId;
					if (!saveConfigToFile(configFilePath))
					{
						LOG_WARN("无法写回配置文件, 但继续使用生成的 client_id: {}", clientId);
					}
					else
					{
						LOG_DEBUG("已生成并保存新的 MQTT Client ID: {}", clientId);
					}
				}
				else
				{
					appConfig.mqttClientId = clientId;
				}
			}
			else
			{
				_STD string clientId { generateUuidWithoutDashes() };
				appConfig.mqttClientId = clientId;

				if (!configNode["mqtt"])
				{
					configNode["mqtt"] = YAML::Node();
				}
				configNode["mqtt"]["client_id"] = clientId;

				if (!saveConfigToFile(configFilePath))
				{
					LOG_WARN("无法写回配置文件, 但继续使用生成的 client_id: {}", clientId);
				}
				else
				{
					LOG_INFO("已生成并保存新的 MQTT Client ID: {}", clientId);
				}
			}

			return true;
		}
		catch (const _STD exception& e)
		{
			LOG_ERROR("验证和修复配置时发生异常: {}", e.what());
			return false;
		}
	}

	_STD string ConfigManager::generateUuidWithoutDashes(void) noexcept
	{
		_STD random_device rd {};
		_STD mt19937	   gen(rd());
		_STD uniform_int_distribution<> dis(0, 15);
		_STD uniform_int_distribution<> dis2(8, 11);

		_STD stringstream				ss {};
		ss << _STD						hex;

		for (int i { 0 }; i < 32; i++)
		{
			if (i == 8 || i == 12 || i == 16 || i == 20)
			{
				continue;
			}

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

	bool ConfigManager::saveConfigToFile(const _STD string& filepath) const noexcept
	{
		try
		{
			_STD ofstream fout(filepath);
			fout << configNode;
			fout.close();
			LOG_DEBUG("配置已写回文件: {}", filepath);
			return true;
		}
		catch (const _STD exception& e)
		{
			LOG_ERROR("写回配置文件失败: {}", e.what());
			return false;
		}
	}

	_STD string ConfigManager::getMqttUrl(void) const noexcept
	{
		if (loaded && !appConfig.mqttUrl.empty())
		{
			return appConfig.mqttUrl;
		}
		LOG_WARN("配置未加载或 MQTT URL 无效, 返回空字符串");
		return "";
	}

	_STD string ConfigManager::getMqttClientId(void) const noexcept
	{
		if (loaded && !appConfig.mqttClientId.empty())
		{
			return appConfig.mqttClientId;
		}
		LOG_WARN("配置未加载或 MQTT Client ID 无效, 返回空字符串");
		return "";
	}

	_STD string ConfigManager::getPlaneCode(void) const noexcept
	{
		if (loaded && !appConfig.planeCode.empty())
		{
			return appConfig.planeCode;
		}
		LOG_WARN("配置未加载或 Plane Code 无效, 返回空字符串");
		return "";
	}
} // namespace plane::config

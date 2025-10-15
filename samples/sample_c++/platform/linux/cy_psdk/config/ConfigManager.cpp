// cy_psdk/cinfig/ConfigManager.cpp

#include "ConfigManager.h"

#include "utils/Logger.h"

#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>

namespace plane::config
{
	ConfigManager& ConfigManager::getInstance(void) noexcept
	{
		static ConfigManager instance {};
		return instance;
	}

	bool ConfigManager::loadAndCheck(_STD_FS path filePath) noexcept
	{
		if (this->loaded_)
		{
			return true;
		}

		if (filePath.empty())
		{
			LOG_DEBUG("未提供配置文件路径，将自动查找可执行文件目录下的 'config.yml'");
			try
			{
				filePath = _STD_FS read_symlink("/proc/self/exe").parent_path() / "config.yml";
				LOG_INFO("自动检测到配置文件路径为: {}", filePath.string());
			}
			catch (const _STD_FS filesystem_error& e)
			{
				LOG_ERROR("自动获取可执行文件路径失败: {}", e.what());
				return false;
			}
		}

		if (this->app_config_.mqttClientId.empty())
		{
			this->app_config_.mqttClientId = this->generateUniqueClientId();
			LOG_DEBUG("运行时 MQTT Client ID 已生成: {}", this->app_config_.mqttClientId);
		}

		try
		{
			if (_STD ifstream file(filePath); !file.good())
			{
				LOG_ERROR("配置文件未找到: {}", filePath.string());
				return false;
			}

			this->config_node_ = _YAML LoadFile(filePath);
			LOG_DEBUG("成功加载配置文件: {}", filePath.string());

			if (!this->validateConfig())
			{
				LOG_ERROR("配置验证失败");
				return false;
			}

			this->loaded_ = true;

			LOG_INFO("配置文件 '{}' 加载并验证成功", filePath.string());
			return true;
		}
		catch (const _YAML Exception& e)
		{
			LOG_ERROR("解析 YAML 配置文件 '{}' 失败: {}", filePath.string(), e.what());
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

			if (this->config_node_["features"])
			{
				const auto& features				= this->config_node_["features"];

				this->app_config_.enableFullPSDK	= features["enable_full_psdk"].as<bool>(false);
				this->app_config_.enableDebugLog	= features["enable_debug_log"].as<bool>(false);
				this->app_config_.enableSkipRC		= features["skip_rc"].as<bool>(false);
				this->app_config_.enableUseTestKmz	= features["use_test_kmz"].as<bool>(false);
				this->app_config_.enableSaveKmzFile = features["save_kmz_file"].as<bool>(false);

				LOG_INFO("功能开关配置加载: FullPSDK={}, DebugLog={}, SkipRC={}, TestKMZ={}, SaveKMZ={}",
						 this->app_config_.enableFullPSDK,
						 this->app_config_.enableDebugLog,
						 this->app_config_.enableSkipRC,
						 this->app_config_.enableUseTestKmz,
						 this->app_config_.enableSaveKmzFile);
			}
			else
			{
				LOG_INFO("配置文件中未找到 'features' 部分，所有功能开关将使用默认值 (false)。");
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

	bool ConfigManager::isStandardProceduresEnabled(void) const noexcept
	{
		return this->loaded_ && this->app_config_.enableFullPSDK;
	}

	bool ConfigManager::isLogLevelDebug(void) const noexcept
	{
		return this->loaded_ && this->app_config_.enableDebugLog;
	}

	bool ConfigManager::isSkipRC(void) const noexcept
	{
		return this->loaded_ && this->app_config_.enableSkipRC;
	}

	bool ConfigManager::isTestKmzFile(void) const noexcept
	{
		return this->loaded_ && this->app_config_.enableUseTestKmz;
	}

	bool ConfigManager::isSaveKmz(void) const noexcept
	{
		return this->loaded_ && this->app_config_.enableSaveKmzFile;
	}
} // namespace plane::config

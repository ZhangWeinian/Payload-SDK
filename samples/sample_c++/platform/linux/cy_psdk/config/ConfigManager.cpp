// cy_psdk/cinfig/ConfigManager.cpp

#include "ConfigManager.h"

#include "utils/Logger.h"

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <array>
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
			this->app_config_.mqttClientId = this->getNewGenerateUniqueClientId();
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
				_STD string plane_code { this->config_node_["plane"]["code"].as<_STD string>() };
				if (plane_code.empty())
				{
					LOG_ERROR("Plane Code 不能为空");
					return false;
				}
				this->app_config_.planeCode = plane_code;
			}
			else
			{
				LOG_ERROR("配置文件中缺少 'plane.code'");
				return false;
			}

			if (this->config_node_["features"])
			{
				const auto& features				  = this->config_node_["features"];

				this->app_config_.enableFullPSDK	  = features["enable_full_psdk"].as<bool>(false);
				this->app_config_.enableTraceLogLevel = features["enable_trace_log"].as<bool>(false);
				this->app_config_.enableSkipRC		  = features["skip_rc"].as<bool>(false);
				this->app_config_.enableSaveKmzFile	  = features["save_kmz_file"].as<bool>(false);
				this->app_config_.enableUseTestKmz	  = features["use_test_kmz"].as<bool>(false);
				this->app_config_.testKmzFilePath	  = features["test_kmz_file_path"].as<_STD string>("/tmp/kmz/1.kmz");

				LOG_TRACE("功能开关配置加载详情: \n"
						  "    FullPSDK={}\n"
						  "    TraceLog={}\n"
						  "    SkipRC={}\n"
						  "    SaveKMZ={}\n"
						  "    TestKMZ={}\n"
						  "    TestKMZPath={}",
						  this->app_config_.enableFullPSDK,
						  this->app_config_.enableTraceLogLevel,
						  this->app_config_.enableSkipRC,
						  this->app_config_.enableSaveKmzFile,
						  this->app_config_.enableUseTestKmz,
						  this->app_config_.testKmzFilePath);
			}
			else
			{
				LOG_WARN("配置文件中未找到 'features' 部分，所有功能开关将使用默认值。");
			}

			return true;
		}
		catch (const _STD exception& e)
		{
			LOG_ERROR("验证配置时发生异常: {}", e.what());
			return false;
		}
	}

	_STD string ConfigManager::getNewGenerateUniqueClientId(void) noexcept
	{
		try
		{
			_BOOST uuids::uuid uuid { _BOOST uuids::random_generator {}() };
			_STD string		   uuid_str { _BOOST uuids::to_string(uuid) };
			uuid_str.erase(_STD remove(uuid_str.begin(), uuid_str.end(), '-'), uuid_str.end());
			return _FMT format("cv_{}", uuid_str);
		}
		catch (const _STD exception& e)
		{
			LOG_ERROR("生成 MQTT Client ID 时发生异常: {}", e.what());
		}
		catch (...)
		{
			LOG_ERROR("生成 MQTT Client ID 时发生未知异常");
		}
		LOG_WARN("使用回退的 MQTT Client ID");
		return "cv_fallback_client_id";
	}

	_STD string ConfigManager::getMqttUrl(void) const noexcept
	{
		return this->getConfigValue(this->app_config_.mqttUrl);
	}

	_STD string ConfigManager::getMqttClientId(void) const noexcept
	{
		return this->getConfigValue(this->app_config_.mqttClientId);
	}

	_STD string ConfigManager::getPlaneCode(void) const noexcept
	{
		return this->getConfigValue(this->app_config_.planeCode);
	}

	bool ConfigManager::isStandardProceduresEnabled(void) const noexcept
	{
		return this->getConfigValue(this->app_config_.enableFullPSDK);
	}

	bool ConfigManager::isTraceLogLevel(void) const noexcept
	{
		return this->getConfigValue(this->app_config_.enableTraceLogLevel);
	}

	bool ConfigManager::isSkipRC(void) const noexcept
	{
		return this->getConfigValue(this->app_config_.enableSkipRC);
	}

	bool ConfigManager::isTestKmzFile(void) const noexcept
	{
		return this->getConfigValue(this->app_config_.enableUseTestKmz);
	}

	bool ConfigManager::isSaveKmz(void) const noexcept
	{
		return this->getConfigValue(this->app_config_.enableSaveKmzFile);
	}

	_STD string_view ConfigManager::getTestKmzFilePath(void) const noexcept
	{
		return this->getConfigValue(this->app_config_.testKmzFilePath);
	}

	template<typename ValueType, typename DefaultType>
	_NODISCARD _STD common_type_t<ValueType, DefaultType> ConfigManager::getConfigValue(const ValueType&   value_if_loaded,
																						const DefaultType& default_value) const noexcept
	{
		if (this->loaded_)
		{
			return value_if_loaded;
		}
		else
		{
			LOG_WARN("配置未加载，返回配置的默认值: {}", default_value);
			return default_value;
		}
	}
} // namespace plane::config

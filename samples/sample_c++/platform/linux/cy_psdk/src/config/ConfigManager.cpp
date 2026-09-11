// cy_psdk/config/ConfigManager.cpp

#include "config/ConfigManager.h"

#include "utils/EXEHomePath.h"
#include "utils/log_util/Logger.h"

#include <fmt/format.h>

#include <fstream>
#include <random>

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
				filePath = plane::utils::getEXEHomePath("config.yml");
				LOG_INFO("自动检测到配置文件路径为: {}", filePath.string());
			}
			catch (const _STD_FS filesystem_error& e)
			{
				LOG_ERROR("自动获取可执行文件路径失败: {}", e.what());
				return false;
			}
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

			if (this->mqtt_client_id_.empty())
			{
				this->mqtt_client_id_ = this->getNewGenerateUniqueClientId();
				LOG_DEBUG("运行时 MQTT Client ID 已生成: {}", this->mqtt_client_id_);
			}

			this->loaded_ = true;

			// 关键配置可见性提示
			if (this->getPlaneCode().empty())
			{
				LOG_INFO("未配置 'plane.code'; 设备标识将由 PSDK 飞控序列号提供 (未取得前目录注册等待)");
			}
			if (!this->config_node_["features"])
			{
				LOG_WARN("配置文件中未找到 'features' 部分, 功能开关将使用缺省值");
			}
			if (!this->config_node_["catalog"])
			{
				LOG_WARN("配置文件中未找到 'catalog' 部分, 目录发现将不启动");
			}
			else
			{
				const auto	targets { this->getCatalogTargets() };
				_STD string targets_text {};
				for (_STD size_t index { 0 }; index < targets.size(); ++index)
				{
					if (index > 0)
					{
						targets_text += ",";
					}
					targets_text += targets[index];
				}
				LOG_DEBUG(
					"SwarmCatalog 发现配置: node_id='{}', port={}, targets=[{}]",
					this->getCatalogNodeId(),
					this->getCatalogDiscoveryPort(),
					targets_text
				);
			}

			LOG_INFO("配置文件 '{}' 加载成功", filePath.string());
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

	_STD string ConfigManager::getNewGenerateUniqueClientId(void) noexcept
	{
		try
		{
			// 使用标准库生成 32 位十六进制随机串作为 Client ID
			_STD random_device rd {};
			_STD mt19937_64	   gen { (static_cast<_STD uint64_t>(rd()) << 32) ^ rd() };
			_STD uniform_int_distribution<int> dist { 0, 15 };
			constexpr _STD string_view		   hex_chars { "0123456789abcdef" };
			_STD string						   uuid_str {};
			uuid_str.reserve(32);
			for (int i { 0 }; i < 32; ++i)
			{
				uuid_str.push_back(hex_chars[static_cast<_STD size_t>(dist(gen))]);
			}
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

	_STD string ConfigManager::getMqttClientId(void) const noexcept
	{
		return this->mqtt_client_id_;
	}

	_STD string_view ConfigManager::getPlaneCode(void) const noexcept
	{
		return this->readValue<_STD string_view>("plane", "code", "");
	}

	bool ConfigManager::isStandardProceduresEnabled(void) const noexcept
	{
		return this->readValue<bool>("features", "enable_full_psdk", false);
	}

	bool ConfigManager::isStatusBoardEnabled(void) const noexcept
	{
		return this->readValue<bool>("features", "enable_status_board", true);
	}

	bool ConfigManager::isTraceLogLevel(void) const noexcept
	{
		return this->readValue<bool>("features", "enable_trace_log", false);
	}

	_DJI E_DjiLoggerConsoleLogLevel ConfigManager::getPsdkLogLevel(void) const noexcept
	{
		auto mapLogLevel = [](int level) -> _DJI E_DjiLoggerConsoleLogLevel
		{
			switch (level)
			{
				case 0:
					return _DJI DJI_LOGGER_CONSOLE_LOG_LEVEL_ERROR;
				case 1:
					return _DJI DJI_LOGGER_CONSOLE_LOG_LEVEL_WARN;
				case 2:
					return _DJI DJI_LOGGER_CONSOLE_LOG_LEVEL_INFO;
				case 3:
					return _DJI DJI_LOGGER_CONSOLE_LOG_LEVEL_DEBUG;
				default:
					return _DJI DJI_LOGGER_CONSOLE_LOG_LEVEL_DEBUG;
			}
		};

		return mapLogLevel(this->readValue<int>("features", "set_psdk_log_level", 3));
	}

	bool ConfigManager::isSkipRC(void) const noexcept
	{
		return this->readValue<bool>("features", "skip_rc", false);
	}

	bool ConfigManager::isSaveKmz(void) const noexcept
	{
		return this->readValue<bool>("features", "save_kmz_file", false);
	}

	double ConfigManager::getTakeoffLatitudeDeg(void) const noexcept
	{
		return this->readValue<double>("plane", "takeoff_lat", 0.0);
	}

	double ConfigManager::getTakeoffLongitudeDeg(void) const noexcept
	{
		return this->readValue<double>("plane", "takeoff_lon", 0.0);
	}

	double ConfigManager::getTakeoffAltitudeM(void) const noexcept
	{
		return this->readValue<double>("plane", "takeoff_alt", 0.0);
	}

	_STD string ConfigManager::getCatalogNodeId(void) const noexcept
	{
		return this->readValue<_STD string>("catalog", "node_id", "");
	}

	_STD uint16_t ConfigManager::getCatalogDiscoveryPort(void) const noexcept
	{
		return this->readValue<_STD uint16_t>("catalog", "port", 30'906);
	}

	_STD vector<_STD string> ConfigManager::getCatalogTargets(void) const noexcept
	{
		_STD vector<_STD string> targets {};
		try
		{
			const auto node { this->config_node_["catalog"]["targets"] };
			if (!node || !node.IsSequence())
			{
				return targets;
			}

			for (const auto& item : node)
			{
				if (_STD string value { item.as<_STD string>("") }; !value.empty())
				{
					targets.push_back(_STD move(value));
				}
			}
		}
		catch (...)
		{
			targets.clear();
		}
		return targets;
	}

	_STD string ConfigManager::getCatalogVersion(void) const noexcept
	{
		// 注册版本: 代码固定契约, 非本地配置项
		return "3.1.0";
	}

	bool ConfigManager::isCatalogBrokerDiscoveryEnabled(void) const noexcept
	{
		// 契约: MQTT broker 一律由目录服务发现 (catalog READY 后生效)
		return true;
	}

	_STD string_view ConfigManager::getCatalogBrokerServiceId(void) const noexcept
	{
		// 契约: 中心 MQTT broker 服务 id
		return "swarm.mqtt.base";
	}

	_STD string_view ConfigManager::getCatalogBrokerPortProtocol(void) const noexcept
	{
		// 契约: 中心 MQTT broker 端点协议
		return "tcp";
	}
} // namespace plane::config

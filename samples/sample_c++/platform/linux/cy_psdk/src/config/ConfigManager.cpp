// cy_psdk/cinfig/ConfigManager.cpp

#include "config/ConfigManager.h"

#include "utils/EXEHomePath.h"
#include "utils/log_util/Logger.h"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>

namespace
{
	// 占位假 SN (16 位 DJI 风格, 兼作"内部代码"), 代码内置不允许配置。
	// TODO: 接入 PSDK 真序列号后替换。同一值用于:
	//   - catalog service_id = "swarm.agent.<SN>" (对齐 msdk)
	//   - catalog service_name = "DJI-PSDK-<内部代码>" (内部代码后续改接后台返回的 internalPlaneId)
	//   - 遥测/指令消息中的飞行器标识 (getPlaneCode 消费点)
	constexpr _STD string_view kPlaneSn { "0A1B2C3D4E5F6078" };
} // namespace

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
		using namespace _STD literals;
		try
		{
			// mqtt.url 不再必填: broker 地址由 SwarmCatalog 服务发现 (swarm.mqtt.base) 提供; 若仍配置则作为静态回退
			if (this->config_node_["mqtt"] && this->config_node_["mqtt"]["url"])
			{
				_STD string_view url { this->config_node_["mqtt"]["url"].as<_STD string_view>() };
				if (url.empty())
				{
					LOG_WARN("配置中 'mqtt.url' 为空, 忽略 (MQTT 地址将仅由 SwarmCatalog 服务发现提供)");
				}
				else
				{
					this->app_config_.mqttUrl = url;
				}
			}
			else
			{
				LOG_INFO("未配置 'mqtt.url', MQTT 地址将由 SwarmCatalog 服务发现 (swarm.mqtt.base) 提供");
			}

			// plane.code 不再必填: 当前阶段使用内置占位 SN (后续改由 PSDK 真序列号填充); 若仍配置则覆盖占位值
			if (this->config_node_["plane"] && this->config_node_["plane"]["code"])
			{
				_STD string_view plane_code { this->config_node_["plane"]["code"].as<_STD string_view>() };
				if (!plane_code.empty())
				{
					this->app_config_.planeCode = plane_code;
				}
			}
			else
			{
				LOG_INFO("未配置 'plane.code', 使用内置占位 SN ({})", kPlaneSn);
			}

			if (this->config_node_["features"])
			{
				const auto& features				  = this->config_node_["features"];

				this->app_config_.enableFullPSDK	  = features["enable_full_psdk"].as<bool>(false);
				this->app_config_.enableTraceLogLevel = features["enable_trace_log"].as<bool>(false);
				this->app_config_.psdkLogLevel		  = features["set_psdk_log_level"].as<_STD uint8_t>(3);
				this->app_config_.enableSkipRC		  = features["skip_rc"].as<bool>(false);
				this->app_config_.enableSaveKmzFile	  = features["save_kmz_file"].as<bool>(false);
				this->app_config_.enableStatusBoard	  = features["enable_status_board"].as<bool>(true);

				LOG_TRACE(
					"功能开关配置加载详情: \n"
					"    FullPSDK={}\n"
					"    TraceLog={}\n"
					"    SkipRC={}\n"
					"    SaveKMZ={}\n"
					"    StatusBoard={}",
					this->app_config_.enableFullPSDK,
					this->app_config_.enableTraceLogLevel,
					this->app_config_.enableSkipRC,
					this->app_config_.enableSaveKmzFile,
					this->app_config_.enableStatusBoard
				);
			}
			else
			{
				LOG_WARN("配置文件中未找到 'features' 部分，所有功能开关将使用默认值");
			}

			// SwarmCatalog 发现配置 (接入始终启用; 心跳与状态上报固定 3s, 不再配置)
			// 注: 注册身份(service_id/service_name/version)与 broker 发现目标已固定于代码, 不允许配置。
			if (this->config_node_["catalog"])
			{
				const auto& catalog = this->config_node_["catalog"];
				auto&		cfg		= this->app_config_.catalog;

				if (catalog["node_id"])
				{
					cfg.node_id = catalog["node_id"].as<_STD string>("");
				}
				if (catalog["port"])
				{
					cfg.port = catalog["port"].as<_STD uint16_t>(30'906);
				}
				if (catalog["targets"] && catalog["targets"].IsSequence())
				{
					cfg.targets.clear();
					for (const auto& target : catalog["targets"])
					{
						const _STD string value { target.as<_STD string>("") };
						if (!value.empty())
						{
							cfg.targets.push_back(value);
						}
					}
				}

				_STD string targets_text {};
				for (_STD size_t index { 0 }; index < cfg.targets.size(); ++index)
				{
					if (index > 0)
					{
						targets_text += ",";
					}
					targets_text += cfg.targets[index];
				}
				LOG_DEBUG("SwarmCatalog 发现配置: node_id='{}', port={}, targets=[{}]", cfg.node_id, cfg.port, targets_text);
			}
			else
			{
				LOG_WARN("配置文件中未找到 'catalog' 部分, 目录发现将不启动");
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
		using namespace _STD literals;

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

	_STD string_view ConfigManager::getMqttUrl(void) const noexcept
	{
		return this->getConfigValue(this->app_config_.mqttUrl);
	}

	_STD string ConfigManager::getMqttClientId(void) const noexcept
	{
		return this->getConfigValue(this->app_config_.mqttClientId);
	}

	_STD string_view ConfigManager::getPlaneCode(void) const noexcept
	{
		// 未配置 plane.code 时回退到内置占位 SN (后续改由 PSDK 真序列号填充)
		const auto& code { this->getConfigValue(this->app_config_.planeCode) };
		return code.empty() ? kPlaneSn : code;
	}

	bool ConfigManager::isStandardProceduresEnabled(void) const noexcept
	{
		return this->getConfigValue(this->app_config_.enableFullPSDK);
	}

	bool ConfigManager::isStatusBoardEnabled(void) const noexcept
	{
		return this->getConfigValue(this->app_config_.enableStatusBoard);
	}

	bool ConfigManager::isTraceLogLevel(void) const noexcept
	{
		return this->getConfigValue(this->app_config_.enableTraceLogLevel);
	}

	_DJI E_DjiLoggerConsoleLogLevel ConfigManager::getPsdkLogLevel(void) const noexcept
	{
		auto mapLogLevel = [](_STD uint8_t level) -> _DJI E_DjiLoggerConsoleLogLevel
		{
			switch (level)
			{
				case 0:
				{
					return _DJI DJI_LOGGER_CONSOLE_LOG_LEVEL_ERROR;
				}
				case 1:
				{
					return _DJI DJI_LOGGER_CONSOLE_LOG_LEVEL_WARN;
				}
				case 2:
				{
					return _DJI DJI_LOGGER_CONSOLE_LOG_LEVEL_INFO;
				}
				case 3:
				{
					return _DJI DJI_LOGGER_CONSOLE_LOG_LEVEL_DEBUG;
				}
				default:
				{
					return _DJI DJI_LOGGER_CONSOLE_LOG_LEVEL_DEBUG;
				}
			}
		};

		return mapLogLevel(this->getConfigValue(this->app_config_.psdkLogLevel, 3));
	}

	bool ConfigManager::isSkipRC(void) const noexcept
	{
		return this->getConfigValue(this->app_config_.enableSkipRC);
	}

	bool ConfigManager::isSaveKmz(void) const noexcept
	{
		return this->getConfigValue(this->app_config_.enableSaveKmzFile);
	}

	_STD string ConfigManager::getCatalogNodeId(void) const noexcept
	{
		return this->app_config_.catalog.node_id;
	}

	_STD uint16_t ConfigManager::getCatalogDiscoveryPort(void) const noexcept
	{
		return this->app_config_.catalog.port;
	}

	const _STD vector<_STD string>& ConfigManager::getCatalogTargets(void) const noexcept
	{
		return this->app_config_.catalog.targets;
	}

	_STD string ConfigManager::getCatalogServiceId(void) const noexcept
	{
		// 固定格式 (对齐 msdk): "swarm.agent.<SN>"; SN 当前为内置占位, 后续接 PSDK 真序列号
		return _FMT format("swarm.agent.{}", this->getPlaneCode());
	}

	_STD string ConfigManager::getCatalogServiceName(void) const noexcept
	{
		// 固定格式: "DJI-PSDK-<内部代码>"; 内部代码当前与占位 SN 同源, 后续改接后台返回的 internalPlaneId
		return _FMT format("DJI-PSDK-{}", this->getPlaneCode());
	}

	_STD string ConfigManager::getCatalogVersion(void) const noexcept
	{
		// 版本固定于代码, 不允许配置
		return "3.1.0";
	}

	bool ConfigManager::isCatalogBrokerDiscoveryEnabled(void) const noexcept
	{
		// 固定启用: MQTT broker 一律由目录服务发现 (catalog READY 后生效), 不允许配置
		return true;
	}

	_STD string_view ConfigManager::getCatalogBrokerServiceId(void) const noexcept
	{
		// 固定为"中心"MQTT broker (对齐 msdk), 不允许配置
		return "swarm.mqtt.base";
	}

	_STD string_view ConfigManager::getCatalogBrokerPortProtocol(void) const noexcept
	{
		// 固定按 tcp 解析 (对齐 msdk), 不允许配置
		return "tcp";
	}

	template<typename ValueType, typename DefaultType>
	_NODISCARD _STD common_type_t<ValueType, DefaultType>
					ConfigManager::getConfigValue(const ValueType& value_if_loaded, const DefaultType& default_value) const noexcept
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

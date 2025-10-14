// cy_psdk/services/PSDK/PSDKManager.cpp

#include "PSDKManager.h"

#include <dji_flight_controller.h>
#include <dji_hms_manager.h>
#include <dji_logger.h>
#include <dji_platform.h>

#include "application.hpp"

#include "config/ConfigManager.h"
#include "services/PSDK/PSDKAdapter.h"
#include "utils/DjiErrorUtils.h"
#include "utils/Logger.h"

#include <chrono>
#include <string>

namespace plane::services
{
	namespace
	{
		_DJI T_DjiReturnCode psdkLogRedirectCallback(const _STD uint8_t* data, _STD uint16_t dataLen)
		{
			_STD string_view message(reinterpret_cast<const char*>(data), dataLen);
			if (!message.empty() && (message.back() == '\n' || message.back() == '\r'))
			{
				message.remove_suffix(1);
			}
			LOG_INFO("[PSDK] {}", message);
			return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
		}
	} // namespace

	PSDKManager& PSDKManager::getInstance(void) noexcept
	{
		static PSDKManager instance {};
		return instance;
	}

	PSDKManager::PSDKManager(void) noexcept = default;

	PSDKManager::~PSDKManager(void) noexcept
	{
		try
		{
			this->stop();
		}
		catch (const _STD exception& e)
		{
			LOG_ERROR("PSDKManager 析构异常: {}", e.what());
		}
		catch (...)
		{
			LOG_ERROR("PSDKManager 析构发生未知异常: <non-std exception>");
		}
	}

	bool PSDKManager::start(int argc, char* argv[])
	{
		if (bool expected { false }; !this->running_.compare_exchange_strong(expected, true))
		{
			LOG_WARN("PSDKManager::start() 被重复调用，已忽略。");
			return true;
		}

		try
		{
			LOG_INFO("--- PSDK 底层服务初始化开始 ---");

			_DJI T_DjiLoggerConsole		console = { 0 };
			console.consoleLevel				= _DJI DJI_LOGGER_CONSOLE_LOG_LEVEL_DEBUG;
			console.func						= _UNNAMED	   psdkLogRedirectCallback;
			console.isSupportColor				= true;

			if (_DJI DjiLogger_AddConsole(&console) != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
			{
				LOG_WARN("重定向 PSDK 日志失败。可能会看到重复或格式不一的日志。");
			}
			else
			{
				LOG_INFO("PSDK 日志已成功重定向到 spdlog 。");
			}

			this->dji_application_ = _STD make_unique<_DJI Application>(argc, argv);

			LOG_INFO("DJI PSDK Application 初始化完成。");

			if (_DJI T_DjiReturnCode returnCode { _DJI DjiHmsManager_Init() }; returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
			{
				LOG_WARN("HMS 模块初始化失败, 错误: {}", plane::utils::djiReturnCodeToString(returnCode));
			}
			LOG_INFO("HMS 模块初始化完成。");

			if (!plane::services::PSDKAdapter::getInstance().setup())
			{
				LOG_ERROR("PSDK 适配器 setup 失败！");
				return false;
			}
			LOG_INFO("PSDK 适配器 setup 完成。");

			if (const auto& config { plane::config::ConfigManager::getInstance() }; config.isStandardProceduresEnabled() && config.isSkipRC())
			{
				if (_DJI T_DjiReturnCode returnCode {
						_DJI DjiFlightController_SetRCLostActionEnableStatus(_DJI DJI_FLIGHT_CONTROLLER_DISABLE_RC_LOST_ACTION) };
					returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
				{
					LOG_WARN("禁用 RC Lost Action 失败，错误: {}, 错误码: {:#08X}", plane::utils::djiReturnCodeToString(returnCode), returnCode);
				}
				else
				{
					LOG_INFO("已成功发送禁用 RC Lost Action 的指令。");
				}
			}

			LOG_INFO("--- PSDK 底层服务初始化成功 ---");
			return true;
		}
		catch (const _STD exception& e)
		{
			LOG_ERROR("PSDK 底层服务初始化异常: {}", e.what());
			this->stop();
			return false;
		}
		catch (...)
		{
			LOG_ERROR("PSDK 底层服务初始化发生未知异常: <non-std exception>");
			this->stop();
			return false;
		}
	}

	void PSDKManager::stop(void)
	{
		if (bool expected { true }; !this->running_.compare_exchange_strong(expected, false))
		{
			return;
		}

		LOG_INFO("--- PSDK 底层服务反初始化开始 ---");

		plane::services::PSDKAdapter::getInstance().cleanup();
		LOG_INFO("PSDK 适配器 cleanup 完成。");

		if (_DJI T_DjiReturnCode returnCode { _DJI DjiHmsManager_DeInit() }; returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
		{
			LOG_WARN("HMS 模块反初始化失败, 错误: {}", plane::utils::djiReturnCodeToString(returnCode));
		}

		this->dji_application_.reset();
		LOG_INFO("DJI PSDK Application 已反初始化。");

		LOG_INFO("--- PSDK 底层服务反初始化完成 ---");
	}
} // namespace plane::services

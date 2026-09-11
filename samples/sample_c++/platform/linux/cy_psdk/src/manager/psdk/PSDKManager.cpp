// cy_psdk/manager/psdk/PSDKManager.cpp

#include "manager/psdk/PSDKManager.h"

#include "application.hpp"
#include <dji_camera_manager.h>
#include <dji_flight_controller.h>
#include <dji_hms_manager.h>
#include <dji_logger.h>
#include <dji_platform.h>

#include "config/ConfigManager.h"
#include "manager/psdk/PSDKAdapter.h"
#include "utils/DjiErrorUtils.h"
#include "utils/log_util/Logger.h"

#include <chrono>
#include <string>

namespace plane::manager
{
	namespace
	{
		// 将 PSDK 日志重定向到 spdlog
		_DJI T_DjiReturnCode psdkLogRedirectCallback(const _STD uint8_t* data, _STD uint16_t dataLen)
		{
			_STD string message(reinterpret_cast<const char*>(data), dataLen);
			while (!message.empty() && (message.back() == '\n' || message.back() == '\r'))
			{
				message.pop_back();
			}
			plane::utils::Logger::getInstance().PSDKLogRedirection(message);
			return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
		}
	} // namespace

	PSDKManager& PSDKManager::getInstance(void) noexcept
	{
		static PSDKManager instance {};
		return instance;
	}

	void PSDKManager::redirectPsdkLogs(void) noexcept
	{
		static _STD atomic<bool> redirected { false };
		if (redirected.load())
		{
			return;
		}

		const auto& config { plane::config::ConfigManager::getInstance() };

		if (_DJI T_DjiLoggerConsole console = { .func			= _UNNAMED psdkLogRedirectCallback,
												.consoleLevel	= static_cast<uint8_t>(config.getPsdkLogLevel()),
												.isSupportColor = true };
			_DJI DjiLogger_AddConsole(&console) != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
		{
			// 失败不置位: 允许平台就绪后再次尝试
			LOG_WARN("重定向 PSDK 日志失败。可能会看到重复或格式不一的日志");
		}
		else
		{
			redirected.store(true);
			LOG_INFO("PSDK 日志已成功重定向到 spdlog");
		}
	}

	PSDKManager::PSDKManager(void) noexcept = default;

	PSDKManager::~PSDKManager(void) noexcept
	{
		try
		{
			LOG_DEBUG("PSDKManager 正在析构");
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
		// 确保幂等性
		if (bool expected { false }; !this->running_.compare_exchange_strong(expected, true))
		{
			LOG_WARN("PSDKManager::start() 被重复调用，已忽略");
			return true;
		}
		else
		{
			LOG_DEBUG("PSDKManager::start() 正在执行");
		}

		// 获取配置管理器实例
		const auto& config { plane::config::ConfigManager::getInstance() };

		try
		{
			LOG_INFO("--- PSDK 底层服务初始化开始 ---");

			// 重定向 PSDK 日志到 spdlog (幂等; 入口通常已提前调用, 此处兜底)
			this->redirectPsdkLogs();

			LOG_INFO("DJI PSDK Application 初始化完成");

			// 初始化 HMS 模块
			if (_DJI T_DjiReturnCode returnCode {
					_DJI DjiHmsManager_Init(),
				};
				returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
			{
				LOG_WARN("HMS 模块初始化失败, 错误: {}", plane::utils::convertDjiError(returnCode));
			}
			LOG_INFO("HMS 模块初始化完成");

			// 初始化相机模块 (读取相机固件/激光测距等; 无相机时失败仅告警)
			if (_DJI T_DjiReturnCode returnCode { _DJI DjiCameraManager_Init() }; returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
			{
				LOG_WARN("相机模块初始化失败 (无相机?), 错误: {}", plane::utils::convertDjiError(returnCode));
			}
			else
			{
				LOG_INFO("相机模块初始化完成");
			}

			// 启动 PSDK 适配器
			if (!plane::manager::PSDKAdapter::getInstance().subscribeTelemetryData())
			{
				LOG_ERROR("PSDK 适配器订阅遥测数据失败！");
				return false;
			}
			LOG_INFO("PSDK 适配器订阅遥测数据完成");

			// 根据配置决定是否禁用遥控器检测
			if (config.isStandardProceduresEnabled() && config.isSkipRC())
			{
				if (_DJI T_DjiReturnCode returnCode {
						_DJI DjiFlightController_SetRCLostActionEnableStatus(_DJI DJI_FLIGHT_CONTROLLER_DISABLE_RC_LOST_ACTION),
					};
					returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
				{
					LOG_WARN("禁用 RC Lost Action 失败，错误: {}, 错误码: {:#08X}", plane::utils::convertDjiError(returnCode), returnCode);
				}
				else
				{
					LOG_INFO("已成功发送禁用 RC Lost Action 的指令");
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
		// 确保幂等性
		if (bool expected { true }; !this->running_.compare_exchange_strong(expected, false))
		{
			return;
		}

		LOG_INFO("--- PSDK 底层服务反初始化开始 ---");

		// 停止 PSDK 适配器
		plane::manager::PSDKAdapter::getInstance().unsubscribeTelemetryData();
		LOG_INFO("PSDK 适配器清理完成");

		// 反初始化 HMS 模块
		if (_DJI T_DjiReturnCode returnCode { _DJI DjiHmsManager_DeInit() }; returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
		{
			LOG_WARN("HMS 模块反初始化失败, 错误: {}", plane::utils::convertDjiError(returnCode));
		}

		// 反初始化相机模块
		if (_DJI T_DjiReturnCode returnCode { _DJI DjiCameraManager_DeInit() }; returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
		{
			LOG_WARN("相机模块反初始化失败, 错误: {}", plane::utils::convertDjiError(returnCode));
		}

		LOG_INFO("DJI PSDK Application 已反初始化");
		LOG_INFO("--- PSDK 底层服务反初始化完成 ---");
	}
} // namespace plane::manager

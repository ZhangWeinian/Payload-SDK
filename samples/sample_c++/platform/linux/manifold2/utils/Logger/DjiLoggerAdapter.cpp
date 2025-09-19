#include "utils/Logger/DjiLoggerAdapter.h"

#include <string>

T_DjiReturnCode DjiLoggerAdapter_ConsoleFunc(const uint8_t* data, uint16_t dataLen)
{
	std::string message(reinterpret_cast<const char*>(data), dataLen);
	LOG_INFO("{}", message);
	return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

void DjiLoggerAdapter_Init()
{
	// 初始化你的Logger
	plane::utils::Logger::getInstance().init();

	// 注册到DJI Logger系统
	T_DjiLoggerConsole console = { .func		   = DjiLoggerAdapter_ConsoleFunc,
								   .consoleLevel   = DJI_LOGGER_CONSOLE_LOG_LEVEL_DEBUG, // 最高级别，接收所有日志
								   .isSupportColor = true };

	DjiLogger_AddConsole(&console);
}

spdlog::level::level_enum DjiLoggerAdapter_MapLevel(E_DjiLoggerConsoleLogLevel djiLevel)
{
	switch (djiLevel)
	{
		case DJI_LOGGER_CONSOLE_LOG_LEVEL_ERROR:
		{
			return spdlog::level::err;
		}
		case DJI_LOGGER_CONSOLE_LOG_LEVEL_WARN:
		{
			return spdlog::level::warn;
		}
		case DJI_LOGGER_CONSOLE_LOG_LEVEL_INFO:
		{
			return spdlog::level::info;
		}
		case DJI_LOGGER_CONSOLE_LOG_LEVEL_DEBUG:
		{
			return spdlog::level::debug;
		}
		default:
		{
			return spdlog::level::info;
		}
	}
}

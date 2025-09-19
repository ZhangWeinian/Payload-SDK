#pragma once

#include <dji_logger.h>

#include "utils/Logger/Logger.h"

#ifdef __cplusplus
extern "C"
{
#endif

	T_DjiReturnCode			  DjiLoggerAdapter_ConsoleFunc(const uint8_t* data, uint16_t dataLen);
	void					  DjiLoggerAdapter_Init();
	spdlog::level::level_enum DjiLoggerAdapter_MapLevel(E_DjiLoggerConsoleLogLevel djiLevel);

#ifdef __cplusplus
}
#endif

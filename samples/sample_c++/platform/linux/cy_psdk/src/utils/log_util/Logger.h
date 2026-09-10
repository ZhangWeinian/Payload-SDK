// cy_psdk/utils/log_util/Logger.h

/*
* 基于 spdlog 的日志系统封装，提供统一的日志接口。
* 支持不同日志级别和格式化输出。
*/

#pragma once

#include "utils/EXEHomePath.h"

#include <fmt/chrono.h>
#include <fmt/format.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <regex>
#include <string>
#include <vector>

#include "define.h"

#define LOG_INFO(fmt, ...)                                                                                                                    \
	plane::utils::Logger::getInstance().log(_SPDLOG source_loc { __FILE__, __LINE__, __FUNCTION__ }, _SPDLOG level::info, fmt, ##__VA_ARGS__)

#define LOG_WARN(fmt, ...)                                                                                                                    \
	plane::utils::Logger::getInstance().log(_SPDLOG source_loc { __FILE__, __LINE__, __FUNCTION__ }, _SPDLOG level::warn, fmt, ##__VA_ARGS__)

#define LOG_ERROR(fmt, ...)                                                                                                                  \
	plane::utils::Logger::getInstance().log(_SPDLOG source_loc { __FILE__, __LINE__, __FUNCTION__ }, _SPDLOG level::err, fmt, ##__VA_ARGS__)

#define LOG_DEBUG(fmt, ...)                                                                                                                    \
	plane::utils::Logger::getInstance().log(_SPDLOG source_loc { __FILE__, __LINE__, __FUNCTION__ }, _SPDLOG level::debug, fmt, ##__VA_ARGS__)

#define LOG_TRACE(fmt, ...)                                                                                                                    \
	plane::utils::Logger::getInstance().log(_SPDLOG source_loc { __FILE__, __LINE__, __FUNCTION__ }, _SPDLOG level::trace, fmt, ##__VA_ARGS__)

#define STD_PRINTLN_ERROR(msg) _STD cerr << "CRITICAL ERROR: " << msg << _STD endl

namespace plane::utils
{
	class Logger
	{
	public:
		static Logger& getInstance(void) noexcept;

		// 初始化日志系统，设置日志输出级别（默认 info 级别）
		void init(_SPDLOG level::level_enum console_level = _SPDLOG level::info) noexcept;

		// 动态调整本地日志记录的级别
		void setLocalLogFileLevel(_SPDLOG level::level_enum level) noexcept;

		// 记录日志，支持格式化参数
		template<typename... Args>
		void log(_SPDLOG source_loc loc, _SPDLOG level::level_enum lvl, _SPDLOG format_string_t<Args...> fmt, Args&&... args) const noexcept
		{
			if (this->logger_)
			{
				this->logger_->log(loc, lvl, fmt, _STD forward<Args>(args)...);
			}
		}

		// 重定向 PSDK 日志输出到本地日志系统
		void PSDKLogRedirection(const _STD string& rawMessage);

	private:
		explicit Logger(void) noexcept			  = default;
		~Logger(void) noexcept					  = default;
		Logger(const Logger&) noexcept			  = delete;
		Logger& operator=(const Logger&) noexcept = delete;

		// 管理日志文件数量，删除最旧的文件以限制总数
		void manageLogFiles(const _STD_FS path& logDir, const _STD_FS path& newLogFile, _STD size_t maxFilesCount);

		_STD shared_ptr<_SPDLOG logger> logger_ {};
	};
} // namespace plane::utils

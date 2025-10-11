// cy_psdk/utils/Logger.h

/*
* 基于 spdlog 的日志系统封装，提供统一的日志接口。
* 支持不同日志级别和格式化输出。
*/

#pragma once

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <iostream>
#include <memory>

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
		static Logger& getInstance(void) noexcept
		{
			static Logger instance {};
			return instance;
		}

		// 初始化日志系统，设置日志输出级别（默认 info 级别）
		// 该函数必须在任何日志调用之前执行
		void init(_SPDLOG level::level_enum console_level = _SPDLOG level::info) noexcept
		{
			if (this->logger_)
			{
				return;
			}

			try
			{
				auto console_sink { _STD make_shared<_SPDLOG sinks::stdout_color_sink_mt>() };
				console_sink->set_level(console_level);
				console_sink->set_pattern("[%m-%d %H:%M:%S.%e] [%^%l%$] [th. %t] [%s:%#] %v");
				this->logger_ = _STD make_shared<_SPDLOG logger>("psdk_logger", console_sink);
				this->logger_->set_level(_SPDLOG level::trace);
				this->logger_->flush_on(_SPDLOG level::trace);
				_SPDLOG set_default_logger(this->logger_);
			}
			catch (const _SPDLOG spdlog_ex& ex)
			{
				STD_PRINTLN_ERROR("日志系统初始化失败: " << ex.what());
			}
		}

		// 记录日志，支持格式化参数
		// loc: 日志调用位置（文件名、行号、函数名）
		// lvl: 日志级别
		// fmt: 格式化字符串
		// args: 格式化参数
		template<typename... Args>
		void log(_SPDLOG source_loc loc, _SPDLOG level::level_enum lvl, _SPDLOG format_string_t<Args...> fmt, Args&&... args) const noexcept
		{
			if (this->logger_)
			{
				this->logger_->log(loc, lvl, fmt, _STD forward<Args>(args)...);
			}
		}

	private:
		explicit Logger(void) noexcept			  = default;
		~Logger(void) noexcept					  = default;
		Logger(const Logger&) noexcept			  = delete;
		Logger& operator=(const Logger&) noexcept = delete;

		_STD shared_ptr<_SPDLOG logger> logger_ {};
	};
} // namespace plane::utils

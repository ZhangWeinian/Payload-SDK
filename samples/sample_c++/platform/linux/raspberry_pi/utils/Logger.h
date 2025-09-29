// raspberry_pi/utils/Logger.h

#pragma once

#include <iostream>
#include <memory>

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include "define.h"

#define LOG_INFO(fmt, ...)                                                                                                                    \
	plane::utils::Logger::getInstance().log(_SPDLOG source_loc { __FILE__, __LINE__, __FUNCTION__ }, _SPDLOG level::info, fmt, ##__VA_ARGS__)

#define LOG_WARN(fmt, ...)                                                                                                                    \
	plane::utils::Logger::getInstance().log(_SPDLOG source_loc { __FILE__, __LINE__, __FUNCTION__ }, _SPDLOG level::warn, fmt, ##__VA_ARGS__)

#define LOG_ERROR(fmt, ...)                                                                                                                  \
	plane::utils::Logger::getInstance().log(_SPDLOG source_loc { __FILE__, __LINE__, __FUNCTION__ }, _SPDLOG level::err, fmt, ##__VA_ARGS__)

#define LOG_DEBUG(fmt, ...)                                                                                                                    \
	plane::utils::Logger::getInstance().log(_SPDLOG source_loc { __FILE__, __LINE__, __FUNCTION__ }, _SPDLOG level::debug, fmt, ##__VA_ARGS__)

#define LOG_CRITICAL_STDERR(msg) _STD cerr << "CRITICAL ERROR: " << msg << _STD endl

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

		void init(_SPDLOG level::level_enum console_level = _SPDLOG level::info) noexcept
		{
			if (logger_)
			{
				return;
			}

			try
			{
				auto console_sink { _STD make_shared<_SPDLOG sinks::stdout_color_sink_mt>() };
				console_sink->set_level(console_level);
				console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [thread %t] [%s:%#] %v");

				logger_ = _STD make_shared<_SPDLOG logger>("psdk_logger", console_sink);
				logger_->set_level(_SPDLOG level::trace);
				_SPDLOG set_default_logger(logger_);
				_SPDLOG set_level(_SPDLOG level::trace);
				_SPDLOG flush_on(_SPDLOG level::info);
			}
			catch (const _SPDLOG spdlog_ex& ex)
			{
				LOG_CRITICAL_STDERR("日志系统初始化失败: " << ex.what());
			}
		}

		template<typename... Args>
		void log(_SPDLOG source_loc loc, _SPDLOG level::level_enum lvl, _SPDLOG format_string_t<Args...> fmt, Args&&... args) const noexcept
		{
			if (logger_)
			{
				logger_->log(loc, lvl, fmt, _STD forward<Args>(args)...);
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

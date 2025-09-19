#pragma once

#include "spdlog/spdlog.h"

#include <memory>

// __FILE__ 是文件名, __LINE__ 是行号, __FUNCTION__ 是函数名
#define LOG_INFO(fmt, ...)                                                                                                                    \
	plane::utils::Logger::getInstance().log(spdlog::source_loc { __FILE__, __LINE__, __FUNCTION__ }, spdlog::level::info, fmt, ##__VA_ARGS__)

#define LOG_WARN(fmt, ...)                                                                                                                    \
	plane::utils::Logger::getInstance().log(spdlog::source_loc { __FILE__, __LINE__, __FUNCTION__ }, spdlog::level::warn, fmt, ##__VA_ARGS__)

#define LOG_ERROR(fmt, ...)                                                                                                                  \
	plane::utils::Logger::getInstance().log(spdlog::source_loc { __FILE__, __LINE__, __FUNCTION__ }, spdlog::level::err, fmt, ##__VA_ARGS__)

#define LOG_DEBUG(fmt, ...)                                                                                                                    \
	plane::utils::Logger::getInstance().log(spdlog::source_loc { __FILE__, __LINE__, __FUNCTION__ }, spdlog::level::debug, fmt, ##__VA_ARGS__)

namespace plane::utils
{
	class Logger
	{
	public:
		static Logger& getInstance(void) noexcept;
		void		   init(spdlog::level::level_enum console_level = spdlog::level::info) noexcept;

		template<typename... Args>
		void log(spdlog::source_loc loc, spdlog::level::level_enum lvl, spdlog::format_string_t<Args...> fmt, Args&&... args) const noexcept
		{
			if (logger_)
			{
				logger_->log(loc, lvl, fmt, std::forward<Args>(args)...);
			}
		}

	private:
		Logger(void) noexcept											  = default;
		~Logger(void) noexcept											  = default;
		Logger(const Logger&) noexcept									  = delete;
		Logger&							operator=(const Logger&) noexcept = delete;

		std::shared_ptr<spdlog::logger> logger_ {};
	};
} // namespace plane::utils

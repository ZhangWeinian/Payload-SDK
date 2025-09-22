#include "utils/Logger/Logger.h"

#include "spdlog/sinks/stdout_color_sinks.h"

#include <iostream>

namespace plane::utils
{
	Logger& Logger::getInstance(void) noexcept
	{
		static Logger instance {};
		return instance;
	}

	void Logger::init(spdlog::level::level_enum console_level) noexcept
	{
		if (logger_)
		{
			return;
		}

		try
		{
			auto console_sink { std::make_shared<spdlog::sinks::stdout_color_sink_mt>() };
			console_sink->set_level(console_level);
			console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [thread %t] [%s:%#] %v");

			logger_ = std::make_shared<spdlog::logger>("psdk_logger", console_sink);
			logger_->set_level(spdlog::level::trace);

			spdlog::set_default_logger(logger_);
			spdlog::set_level(spdlog::level::trace);
			spdlog::flush_on(spdlog::level::info);
		}
		catch (const spdlog::spdlog_ex& ex)
		{
			std::cerr << "====================================" << std::endl;
			std::cerr << "           日志初始化失败：" << ex.what() << std::endl;
			std::cerr << "====================================" << std::endl;
		}
	}
} // namespace plane::utils

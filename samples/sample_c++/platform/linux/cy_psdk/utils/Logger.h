// cy_psdk/utils/Logger.h

/*
* 基于 spdlog 的日志系统封装，提供统一的日志接口。
* 支持不同日志级别和格式化输出。
*/

#pragma once

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
				_STD vector<_SPDLOG sink_ptr> sinks {};

				auto						  console_sink { _STD make_shared<_SPDLOG sinks::stdout_color_sink_mt>() };
				console_sink->set_level(console_level);
				console_sink->set_pattern("[%m-%d %H:%M:%S.%e] [%^%l%$] [th.%t] [%s:%#] %v");
				sinks.push_back(console_sink);

				_STD_FS path log_directory("logs");
				_STD_FS		 create_directories(log_directory);

				auto		 now { _STD_CHRONO system_clock::now() };
				_STD string	 timestamp_str { _FMT format("{:%Y%m%d_%H%M%S}", now) };
				_STD_FS path log_filepath { log_directory / _FMT format("psdk_app_{}.log", timestamp_str) };

				auto		 file_sink { _STD make_shared<_SPDLOG sinks::basic_file_sink_mt>(log_filepath.string(), false) };
				file_sink->set_level(_SPDLOG level::trace);
				file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [th.%t] [%s:%#] %v");
				sinks.push_back(file_sink);

				this->logger_ = _STD make_shared<_SPDLOG logger>("psdk_logger", sinks.begin(), sinks.end());
				this->logger_->set_level(_SPDLOG level::trace);
				this->logger_->flush_on(_SPDLOG level::warn);
				_SPDLOG set_default_logger(this->logger_);

				LOG_INFO("日志文件已创建: {}", log_filepath.string());

				this->manageLogFiles(log_directory, log_filepath, 20);
			}
			catch (const _STD exception& ex)
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

		void	manageLogFiles(const _STD_FS path& log_dir, const _STD_FS path& new_log_file, _STD size_t max_files)
		{
			_STD_FS path	latest_link { log_dir / "latest.log" };
			_STD error_code ec {};
			if (_STD_FS exists(latest_link, ec))
			{
				_STD_FS remove(latest_link, ec);
			}

			_STD_FS create_symlink(_STD_FS absolute(new_log_file), latest_link, ec);
			if (ec)
			{
				LOG_WARN("创建日志软链接 'latest.log' 失败: {}", ec.message());
			}

			_STD vector<_STD_FS path> log_files {};
			for (const auto& entry : _STD_FS directory_iterator(log_dir))
			{
				if (entry.is_regular_file() && entry.path().extension() == ".log" && entry.path().filename() != "latest.log")
				{
					log_files.push_back(entry.path());
				}
			}

			if (log_files.size() > max_files)
			{
				_STD sort(log_files.begin(), log_files.end());

				LOG_INFO("日志文件数量 ({}) 已超过最大限制 ({})，正在删除最旧的文件...", log_files.size(), max_files);
				_STD size_t files_to_delete { log_files.size() - max_files };
				for (_STD size_t i { 0 }; i < files_to_delete; ++i)
				{
					_STD_FS remove(log_files[i], ec);
					if (ec)
					{
						LOG_ERROR("删除旧日志文件 '{}' 失败: {}", log_files[i].string(), ec.message());
					}
					else
					{
						LOG_INFO("已删除旧日志文件: {}", log_files[i].string());
					}
				}
			}
		}

		_STD shared_ptr<_SPDLOG logger> logger_ {};
	};
} // namespace plane::utils

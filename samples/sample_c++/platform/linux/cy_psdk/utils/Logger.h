// cy_psdk/utils/Logger.h

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
				// 创建控制台和文件日志接收器
				_STD vector<_SPDLOG sink_ptr> sinks {};
				auto						  console_sink { _STD make_shared<_SPDLOG sinks::stdout_color_sink_mt>() };
				console_sink->set_level(console_level);
				console_sink->set_pattern("[%m-%d %H:%M:%S.%e] [%^%l%$] [th.%t] [%s:%#] %v");
				sinks.push_back(console_sink);

				// 创建日志文件，按时间戳命名以避免覆盖
				_STD_FS path log_directory { plane::utils::getEXEHomePath("logs") };

				// 如果 logs 目录不存在，则创建
				if (!_STD_FS exists(log_directory))
				{
					_STD_FS create_directories(log_directory);
				}

				// 生成基于当前时间的日志文件名
				auto		 now { _STD_CHRONO system_clock::now() };
				_STD string	 timestamp_str { _FMT format("{:%Y%m%d_%H%M%S}", now) };
				_STD_FS path log_filepath { log_directory / _FMT format("psdk_app_{}.log", timestamp_str) };

				// 创建文件日志接收器
				auto file_sink { _STD make_shared<_SPDLOG sinks::basic_file_sink_mt>(log_filepath.string(), false) };
				file_sink->set_level(_SPDLOG level::trace);
				file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [th.%t] [%s:%#] %v");
				sinks.push_back(file_sink);

				// 创建 Logger 实例
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

		// 动态调整本地日志记录的级别
		void setLocalLogFileLevel(_SPDLOG level::level_enum level) noexcept
		{
			if (this->logger_)
			{
				this->logger_->set_level(level);
				LOG_DEBUG("本地记录的日志级别已动态调整为: {}", _SPDLOG level::to_string_view(level));
			}
			else
			{
				STD_PRINTLN_ERROR("请勿在 Logger 初始化之前设置日志级别！");
			}
		}

		// 记录日志，支持格式化参数
		template<typename... Args>
		void log(_SPDLOG source_loc loc, _SPDLOG level::level_enum lvl, _SPDLOG format_string_t<Args...> fmt, Args&&... args) const noexcept
		{
			if (this->logger_)
			{
				this->logger_->log(loc, lvl, fmt, _STD forward<Args>(args)...);
			}
		}

		void PSDKLogRedirection(const _STD string& rawMessage)
		{
			if (!this->logger_)
			{
				return;
			}

			try
			{
				// (?:\x1b\[[0-9;]*m)? : 匹配并忽略开头的 ANSI 颜色码
				// \s*([\d\.]+)        : 捕获组1 - PSDK运行时间戳
				// \s+([^\s]+)         : 捕获组2 - 模块名
				// \s+\[(\w+)\]        : 捕获组3 - 日志级别
				// \s+([^:]+:\d+)      : 捕获组4 - 文件名:行号
				// \s+(.*?)            : 捕获组5 - 实际消息内容
				// (?:\x1b\[0m)?\s*$   : 匹配并忽略结尾的 ANSI 重置码和空格
				static const _STD regex pattern(
					R"((?:\x1b\[[0-9;]*m)?\s*([\d\.]+)\s+([^\s]+)\s+\[(\w+)\]\s+([^:]+:\d+)\s+(.*?)(?:\x1b\[0m)?\s*$)");

				if (_STD smatch matches {}; _STD regex_search(rawMessage, matches, pattern) && matches.size() == 6)
				{
					_STD string psdk_time { matches[1].str() };
					_STD string module { matches[2].str() };
					_STD string level_str { matches[3].str() };
					_STD string file_line { matches[4].str() };
					_STD string content { matches[5].str() };

					// 映射日志级别
					_SPDLOG level::level_enum log_level { _SPDLOG level::info };
					if (level_str == "Error")
					{
						log_level = _SPDLOG level::err;
					}
					else if (level_str == "Warn")
					{
						log_level = _SPDLOG level::warn;
					}
					else if (level_str == "Debug")
					{
						log_level = _SPDLOG level::debug;
					}

					this->logger_->log(log_level, "[PSDK:{}] {} ({})", module, content, file_line);
				}
				else
				{
					// 如果正则匹配失败，则进行简单的去除颜色码处理后输出
					_STD string cleanMsg { _STD regex_replace(rawMessage, _STD regex(R"(\x1b\[[0-9;]*m)"), "") };
					this->logger_->log(_SPDLOG level::info, "[PSDK] {}", cleanMsg);
				}
			}
			catch (...)
			{
				// 防止正则解析异常导致崩溃
				this->logger_->log(_SPDLOG level::info, "[PSDK] {}", rawMessage);
			}
		}

	private:
		explicit Logger(void) noexcept			  = default;
		~Logger(void) noexcept					  = default;
		Logger(const Logger&) noexcept			  = delete;
		Logger& operator=(const Logger&) noexcept = delete;

		// 管理日志文件数量，删除最旧的文件以限制总数
		void manageLogFiles(const _STD_FS path& logDir, const _STD_FS path& newLogFile, _STD size_t maxFilesCount)
		{
			// 创建或更新指向最新日志文件的符号链接
			_STD_FS path	latest_link { plane::utils::getEXEHomePath("latest.log") };
			_STD error_code ec {};
			if (_STD_FS exists(latest_link, ec))
			{
				_STD_FS remove(latest_link, ec);
			}

			// 创建指向最新日志文件的符号链接
			_STD_FS create_symlink(_STD_FS absolute(newLogFile), latest_link, ec);
			if (ec)
			{
				LOG_WARN("创建日志软链接 'latest.log' 失败: {}", ec.message());
			}

			// 检查日志目录中的日志文件数量，删除最旧的文件以限制总数
			_STD vector<_STD_FS path> log_files {};
			for (const auto& entry : _STD_FS directory_iterator(logDir))
			{
				if (entry.is_regular_file() && entry.path().extension() == ".log" && entry.path().filename() != "latest.log")
				{
					log_files.push_back(entry.path());
				}
			}

			// 如果日志文件数量超过限制，则删除最旧的文件
			if (log_files.size() > maxFilesCount)
			{
				_STD sort(log_files.begin(), log_files.end());

				LOG_INFO("日志文件数量 ({}) 已超过最大限制 ({})，正在删除最旧的文件", log_files.size(), maxFilesCount);
				_STD size_t files_to_delete { log_files.size() - maxFilesCount };
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

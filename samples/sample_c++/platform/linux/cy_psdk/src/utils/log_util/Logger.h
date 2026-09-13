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

#define LOG_INFO(fmt, ...)                                                                                                     \
    plane::utils::Logger::getInstance()                                                                                        \
        .log(::spdlog::source_loc { __FILE__, __LINE__, __FUNCTION__ }, ::spdlog::level::info, fmt __VA_OPT__(, ) __VA_ARGS__)

#define LOG_WARN(fmt, ...)                                                                                                     \
    plane::utils::Logger::getInstance()                                                                                        \
        .log(::spdlog::source_loc { __FILE__, __LINE__, __FUNCTION__ }, ::spdlog::level::warn, fmt __VA_OPT__(, ) __VA_ARGS__)

#define LOG_ERROR(fmt, ...)                                                                                                   \
    plane::utils::Logger::getInstance()                                                                                       \
        .log(::spdlog::source_loc { __FILE__, __LINE__, __FUNCTION__ }, ::spdlog::level::err, fmt __VA_OPT__(, ) __VA_ARGS__)

#define LOG_DEBUG(fmt, ...)                                                                                                     \
    plane::utils::Logger::getInstance()                                                                                         \
        .log(::spdlog::source_loc { __FILE__, __LINE__, __FUNCTION__ }, ::spdlog::level::debug, fmt __VA_OPT__(, ) __VA_ARGS__)

#define LOG_TRACE(fmt, ...)                                                                                                     \
    plane::utils::Logger::getInstance()                                                                                         \
        .log(::spdlog::source_loc { __FILE__, __LINE__, __FUNCTION__ }, ::spdlog::level::trace, fmt __VA_OPT__(, ) __VA_ARGS__)

// 低层兜底输出 (logger 未就绪场景); 参数为流续写表达式, 如 "失败: " << ex.what()
#define STD_PRINTLN_ERROR(...) ::std::cerr << "CRITICAL ERROR: " << __VA_ARGS__ << ::std::endl

namespace plane::utils
{
    class Logger
    {
    public:
        static Logger& getInstance(void) noexcept;

        // 初始化日志系统，设置日志输出级别（默认 info 级别）
        void init(::spdlog::level::level_enum console_level = ::spdlog::level::info) noexcept;

        // 动态调整本地日志记录的级别
        void setLocalLogFileLevel(::spdlog::level::level_enum level) noexcept;

        // 记录日志，支持格式化参数
        template<typename... Args>
        void log(
            ::spdlog::source_loc               loc,
            ::spdlog::level::level_enum        lvl,
            ::spdlog::format_string_t<Args...> fmt,
            Args&&... args
        ) const noexcept
        {
            if (this->logger_)
            {
                this->logger_->log(loc, lvl, fmt, ::std::forward<Args>(args)...);
            }
        }

        // 重定向 PSDK 日志输出到本地日志系统
        void PSDKLogRedirection(const ::std::string& rawMessage);

    private:
        explicit Logger(void) noexcept            = default;
        ~Logger(void) noexcept                    = default;
        Logger(const Logger&) noexcept            = delete;
        Logger& operator=(const Logger&) noexcept = delete;

        // 管理日志文件数量，删除最旧的文件以限制总数
        void manageLogFiles(const ::std::filesystem::path& logDir, const ::std::filesystem::path& newLogFile, ::std::size_t maxFilesCount);

		::std::shared_ptr<::spdlog::logger> logger_ {};
    };
} // namespace plane::utils

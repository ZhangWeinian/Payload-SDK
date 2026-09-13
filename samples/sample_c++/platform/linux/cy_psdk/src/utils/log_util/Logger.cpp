// cy_psdk/utils/log_util/Logger.cpp

#include "utils/log_util/Logger.h"

#include "utils/log_util/StatusBoardSink.h"

namespace plane::utils
{
    Logger& Logger::getInstance(void) noexcept
    {
        static Logger instance {};
        return instance;
    }

    void Logger::init(::spdlog::level::level_enum console_level) noexcept
    {
        if (this->logger_)
        {
            return;
        }

        try
        {
            // 创建控制台和文件日志接收器
            ::std::vector<::spdlog::sink_ptr> sinks {};
            // 控制台输出经状态板接收器接入终端状态板: 日志与固定状态块协作,
            // 非交互终端下自动退化为普通 stdout 输出
            auto console_sink { ::std::make_shared<plane::utils::StatusBoardSink>() };
            console_sink->set_level(console_level);
            console_sink->set_pattern("[%m-%d %H:%M:%S.%e] [%^%l%$] [th.%t] [%s:%#] %v");
            sinks.push_back(console_sink);

            // 创建日志文件，按时间戳命名以避免覆盖
            ::std::filesystem::path log_directory { plane::utils::getEXEHomePath("logs") };

            // 如果 logs 目录不存在，则创建
            if (!::std::filesystem::exists(log_directory))
            {
                ::std::filesystem::create_directories(log_directory);
            }

            // 生成基于当前时间的日志文件名
            auto                    now { ::std::chrono::system_clock::now() };
            ::std::string           timestamp_str { ::fmt::format("{:%Y%m%d_%H%M%S}", now) };
            ::std::filesystem::path log_filepath { log_directory / ::fmt::format("psdk_app_{}.log", timestamp_str) };

            // 创建文件日志接收器
            auto file_sink { ::std::make_shared<::spdlog::sinks::basic_file_sink_mt>(log_filepath.string(), false) };
            file_sink->set_level(::spdlog::level::trace);
            file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [th.%t] [%s:%#] %v");
            sinks.push_back(file_sink);

            // 创建 Logger 实例
				this->logger_ = ::std::make_shared<::spdlog::logger>("psdk_logger", sinks.begin(), sinks.end());
            this->logger_->set_level(::spdlog::level::trace);
            this->logger_->flush_on(::spdlog::level::warn);
            ::spdlog::set_default_logger(this->logger_);

            LOG_INFO("日志文件已创建: {}", log_filepath.string());

            this->manageLogFiles(log_directory, log_filepath, 20);
        }
        catch (const ::std::exception& ex)
        {
            STD_PRINTLN_ERROR("日志系统初始化失败: " << ex.what());
        }
    }

    void Logger::setLocalLogFileLevel(::spdlog::level::level_enum level) noexcept
    {
        if (this->logger_)
        {
            this->logger_->set_level(level);
            LOG_DEBUG("本地记录的日志级别已动态调整为: {}", ::spdlog::level::to_string_view(level));
        }
        else
        {
            STD_PRINTLN_ERROR("请勿在 Logger 初始化之前设置日志级别！");
        }
    }

    void Logger::PSDKLogRedirection(const ::std::string& rawMessage)
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
            static const ::std::
                regex pattern(R"((?:\x1b\[[0-9;]*m)?\s*([\d\.]+)\s+([^\s]+)\s+\[(\w+)\]\s+([^:]+:\d+)\s+(.*?)(?:\x1b\[0m)?\s*$)");

            if (::std::smatch matches {}; ::std::regex_search(rawMessage, matches, pattern) && matches.size() == 6)
            {
                ::std::string psdk_time { matches[1].str() };
                ::std::string module { matches[2].str() };
                ::std::string level_str { matches[3].str() };

                // SDK 的 "文件:行号" (如 dji_command.c:910): 拆分为文件名与行号,
                // 作为 spdlog 源位置输出, 与其他日志列对齐 (避免空源位置显示 "[:]")
                ::std::string file_name { matches[4].str() };
                int           file_number { 0 };
                if (const auto colon_pos { file_name.rfind(':') }; colon_pos != ::std::string::npos)
                {
                    try
                    {
                        file_number = ::std::stoi(file_name.substr(colon_pos + 1));
                    }
                    catch (...)
                    {
                        file_number = 0;
                    }
                    file_name.resize(colon_pos);
                }
                ::std::string content { matches[5].str() };

                // 映射日志级别
                ::spdlog::level::level_enum log_level { ::spdlog::level::info };
                if (level_str == "Error")
                {
                    log_level = ::spdlog::level::err;
                }
                else if (level_str == "Warn")
                {
                    log_level = ::spdlog::level::warn;
                }
                else if (level_str == "Debug")
                {
                    log_level = ::spdlog::level::debug;
                }

                this->logger_->log(::spdlog::source_loc { file_name.c_str(), file_number, "" }, log_level, "[PSDK:{}] {}", module, content);
            }
            else
            {
                // 如果正则匹配失败，则进行简单的去除颜色码处理后输出
                ::std::string cleanMsg { ::std::regex_replace(rawMessage, ::std::regex(R"(\x1b\[[0-9;]*m)"), "") };
                this->logger_->log(::spdlog::level::info, "[PSDK] {}", cleanMsg);
            }
        }
        catch (...)
        {
            // 防止正则解析异常导致崩溃
            this->logger_->log(::spdlog::level::info, "[PSDK] {}", rawMessage);
        }
    }

    void Logger::manageLogFiles(const ::std::filesystem::path& logDir, const ::std::filesystem::path& newLogFile, ::std::size_t maxFilesCount)
    {
        // 创建或更新指向最新日志文件的符号链接
        ::std::filesystem::path latest_link { plane::utils::getEXEHomePath("latest.log") };
        ::std::error_code       ec {};
        // 链接可能悬空 (指向已被清理的旧日志), 此时 exists() 返回 false, 需直接移除后重建
        ::std::filesystem::remove(latest_link, ec);
        ec.clear();

        // 创建指向最新日志文件的符号链接
        ::std::filesystem::create_symlink(::std::filesystem::absolute(newLogFile), latest_link, ec);
        if (ec)
        {
            LOG_WARN("创建日志软链接 'latest.log' 失败: {}", ec.message());
        }

        // 检查日志目录中的日志文件数量，删除最旧的文件以限制总数
        ::std::vector<::std::filesystem::path> log_files {};
        for (const auto& entry : ::std::filesystem::directory_iterator(logDir))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".log" && entry.path().filename() != "latest.log")
            {
                log_files.push_back(entry.path());
            }
        }

        // 如果日志文件数量超过限制，则删除最旧的文件
        if (log_files.size() > maxFilesCount)
        {
            ::std::sort(log_files.begin(), log_files.end());

            LOG_INFO("日志文件数量 ({}) 已超过最大限制 ({})，正在删除最旧的文件", log_files.size(), maxFilesCount);
            ::std::size_t files_to_delete { log_files.size() - maxFilesCount };
            for (::std::size_t i { 0 }; i < files_to_delete; ++i)
            {
                ::std::filesystem::remove(log_files[i], ec);
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
} // namespace plane::utils

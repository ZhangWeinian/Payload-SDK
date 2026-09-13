// cy_psdk/utils/EXEHomePath.h

#pragma once

#include <filesystem>

#include "define.h"

namespace plane::utils
{
    class Get_EXE_home_path_fun_: private Not_quite_object_
    {
    public:
        using Not_quite_object_::Not_quite_object_;

        // 在启动最早处用 argv[0] 显式初始化"交付目录"。
        // 注意: 经打包解释器 (ld-linux-*) 显式启动时, /proc/self/exe 指向解释器所在的 libs/,
        //       直接依赖它会导致 config.yml/日志路径全部错位, 因此必须以 argv[0] 为准。
        void init(const char* argv0) noexcept
        {
            ::std::error_code ec;
            if (argv0 != nullptr && argv0[0] != '\0')
            {
                ::std::filesystem::path exe { argv0 };
                if (exe.is_relative())
                {
                    exe = ::std::filesystem::current_path(ec) / exe;
                }
                if (auto canonical { ::std::filesystem::weakly_canonical(exe, ec) }; !ec)
                {
                    this->exe_home_path_ = canonical.parent_path();
                }
            }
        }

        // 获取可执行文件所在目录路径
        [[nodiscard]] ::std::filesystem::path operator()(const ::std::filesystem::path& childPath = "") noexcept
        {
            if (this->exe_home_path_.empty())
            {
                // read_symlink 非抛错重载: /proc 不可用等场景下保持空路径
                ::std::error_code ec;
                if (const auto target { ::std::filesystem::read_symlink("/proc/self/exe", ec) }; !ec)
                {
                    this->exe_home_path_ = target.parent_path();
                }
            }

            return this->exe_home_path_ / childPath;
        }

    private:
        ::std::filesystem::path exe_home_path_ {};
    };

    inline Get_EXE_home_path_fun_ getEXEHomePath { Not_quite_object_::Construct_tag_ {} };
} // namespace plane::utils

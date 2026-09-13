// cy_psdk/manager/catalog/client/internal/discovery/CatalogIpCache.cpp

#include "manager/catalog/client/internal/discovery/CatalogIpCache.h"

#include <system_error>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <unistd.h>

#include "manager/catalog/client/internal/util/TargetExpander.h"
#include "manager/catalog/client/internal/util/TextUtil.h"

#include "define.h"

namespace plane::catalog::internal
{
    CatalogIpCache::CatalogIpCache(::std::string file): file_(::std::move(file)) {}

    ::std::vector<::std::string> CatalogIpCache::prioritize(const ::std::vector<::std::string>& targets)
    {
        ::std::vector<::std::string> result { targets };
        ::std::ifstream              input { this->file_, ::std::ios::binary };
        if (!input.is_open())
        {
            return result;
        }
        ::std::string line {};
        ::std::getline(input, line);
        const ::std::string cached { trimAsciiWhitespaceCopy(line) };
        if (cached.empty())
        {
            return result;
        }
        for (::std::size_t index { 0 }; index < result.size(); ++index)
        {
            if (result[index] == cached && index > 0)
            {
                result.erase(result.begin() + static_cast<::std::ptrdiff_t>(index));
                result.insert(result.begin(), cached);
                break;
            }
        }
        return result;
    }

    void CatalogIpCache::save(const ::std::string& ip)
    {
        // 只保存规范单 IP
        const ::std::vector<::std::string>   single { ip };
        Result<::std::vector<::std::string>> checked { expandTargets(single, 1) };
        if (!checked.has_value())
        {
            return;
        }

        const ::std::filesystem::path path { this->file_ };
        ::std::error_code             ec {};
        const auto                    parent { path.parent_path() };
        if (!parent.empty())
        {
            ::std::filesystem::create_directories(parent, ec);
            if (ec)
            {
                return;
            }
        }

        // 临时文件建在目标目录内 (对齐 java Files.createTempFile(parent, ...)):
        // 与目标同文件系统, 保证 rename 原子生效; fd 直写, 避免二次 close 误关他人 fd
        const ::std::filesystem::path temp_template { path.parent_path() / (path.filename().string() + ".tmp.XXXXXX") };
        const ::std::string           temp_name { temp_template.string() };
        ::std::vector<char>           writable { temp_name.begin(), temp_name.end() };
        writable.push_back('\0');
        const int fd { ::mkstemp(writable.data()) };
        if (fd < 0)
        {
            return;
        }
        const ::std::string temp_path { writable.data() };
        const ::std::string payload { ip + "\n" };
        const ::ssize_t     written { ::write(fd, payload.data(), payload.size()) };
        const bool          write_ok { written == static_cast<::ssize_t>(payload.size()) };
        if (::close(fd) != 0 || !write_ok)
        {
            ::remove(temp_path.c_str());
            return;
        }

        ::std::filesystem::rename(temp_path, path, ec);
        if (ec)
        {
            // 跨文件系统等场景: 回退复制后删除临时文件 (对齐 java REPLACE_EXISTING 回退)
            ::std::error_code fallback_ec {};
            ::std::filesystem::copy_file(temp_path, path, ::std::filesystem::copy_options::overwrite_existing, fallback_ec);
            ::std::filesystem::remove(temp_path, fallback_ec);
        }
    }
} // namespace plane::catalog::internal

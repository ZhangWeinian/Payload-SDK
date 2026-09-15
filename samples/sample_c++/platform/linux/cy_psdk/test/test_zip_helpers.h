// cy_psdk/test/test_zip_helpers.h
//
// 测试共用的 ZIP 读取辅助: 把内存中的 KMZ 当作归档读出某个条目 (libzip, 不落盘)。

#pragma once

#include <zip.h>

#include <cstdint>
#include <string>
#include <vector>

namespace plane::test
{
    inline ::std::string readZipEntry(const ::std::vector<::std::uint8_t>& archive_data, const ::std::string& entryName)
    {
        ::zip_error_t error {};
        ::zip_error_init(&error);

        ::zip_source_t* source { ::zip_source_buffer_create(archive_data.data(), archive_data.size(), 0, &error) };
        if (source == nullptr)
        {
            ::zip_error_fini(&error);
            return {};
        }

        ::zip_t* archive { ::zip_open_from_source(source, ZIP_RDONLY, &error) };
        if (archive == nullptr)
        {
            ::zip_source_free(source);
            ::zip_error_fini(&error);
            return {};
        }

        ::std::string content {};
        if (::zip_file_t * file { ::zip_fopen(archive, entryName.c_str(), 0) })
        {
            char          buffer[4096];
            ::zip_int64_t readCount {};
            while ((readCount = ::zip_fread(file, buffer, sizeof(buffer))) > 0)
            {
                content.append(buffer, static_cast<::std::size_t>(readCount));
            }
            ::zip_fclose(file);
        }

        ::zip_close(archive); // 一并释放 source
        ::zip_error_fini(&error);
        return content;
    }
} // namespace plane::test

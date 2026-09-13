// cy_psdk/manager/catalog/client/internal/util/TextUtil.h
//
// 文本工具 (SDK 内部)。trim 语义对齐 java String.trim():
// 去除两端码点 <= U+0020 的字符, 内部字符原样保留。

#pragma once

#include <string_view>
#include <string>

#include "define.h"

namespace plane::catalog::internal
{
    // 返回去掉首尾空白 (<= 0x20) 的视图 (指向原串)
    [[nodiscard]] inline ::std::string_view trimAsciiWhitespace(::std::string_view value) noexcept
    {
        ::std::size_t begin { 0 };
        ::std::size_t end { value.size() };
        while (begin < end && static_cast<unsigned char>(value[begin]) <= 0X20)
        {
            ++begin;
        }
        while (end > begin && static_cast<unsigned char>(value[end - 1]) <= 0X20)
        {
            --end;
        }
        return value.substr(begin, end - begin);
    }

    // 返回去掉首尾空白后的独立字符串
    [[nodiscard]] inline ::std::string trimAsciiWhitespaceCopy(::std::string_view value)
    {
        return ::std::string { trimAsciiWhitespace(value) };
    }
} // namespace plane::catalog::internal

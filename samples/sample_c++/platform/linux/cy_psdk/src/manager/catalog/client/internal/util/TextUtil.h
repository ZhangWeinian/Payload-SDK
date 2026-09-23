// cy_psdk/manager/catalog/client/internal/util/TextUtil.h
//
// 文本工具 (SDK 内部)。trim 语义对齐 java String.trim():
// 去除两端码点 <= U+0020 的字符, 内部字符原样保留。

#pragma once

#include <string_view>
#include <cstdint>
#include <string>
#include <vector>

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

    // 严格 UTF-8 校验 (拒绝被截断的序列、非法续字节、过长编码、代理区与越界码点)。
    // 对齐 java CharsetDecoder(REPORT) 的判定口径: 数据池 payload 声明为 UTF-8 文本时用它把关。
    [[nodiscard]] inline bool isValidUtf8(const ::std::vector<::std::uint8_t>& data) noexcept
    {
        ::std::size_t index { 0 };
        while (index < data.size())
        {
            const ::std::uint8_t lead { data[index] };
            if (lead <= 0X7f)
            {
                ++index;
                continue;
            }
            ::std::size_t   extra { 0 };
            ::std::uint32_t codepoint { 0 };
            if (lead >= 0Xc2 && lead <= 0Xdf)
            {
                extra     = 1;
                codepoint = lead & 0X1fu;
            }
            else if (lead >= 0Xe0 && lead <= 0Xef)
            {
                extra     = 2;
                codepoint = lead & 0X0fu;
            }
            else if (lead >= 0Xf0 && lead <= 0Xf4)
            {
                extra     = 3;
                codepoint = lead & 0X07u;
            }
            else
            {
                return false; // 0X80-0Xc1 (孤立续字节) 或 0Xf5-0Xff 均为非法起始
            }
            if (index + extra >= data.size())
            {
                return false; // 序列被截断
            }
            for (::std::size_t offset { 1 }; offset <= extra; ++offset)
            {
                const ::std::uint8_t continuation { data[index + offset] };
                if ((continuation & 0Xc0u) != 0X80u)
                {
                    return false;
                }
                codepoint = (codepoint << 6u) | (continuation & 0X3fu);
            }
            if ((extra == 2 && codepoint < 0X800u) || (extra == 3 && codepoint < 0X10000u) || codepoint > 0X10'ff'ffu)
            {
                return false; // 过长编码或超出 Unicode 范围
            }
            if (codepoint >= 0Xd800u && codepoint <= 0Xdfffu)
            {
                return false; // 代理区不允许出现在 UTF-8 中
            }
            index += extra + 1;
        }
        return true;
    }
} // namespace plane::catalog::internal

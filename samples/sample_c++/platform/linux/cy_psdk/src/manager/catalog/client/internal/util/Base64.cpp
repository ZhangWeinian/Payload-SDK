// cy_psdk/manager/catalog/client/internal/util/Base64.cpp

#include "manager/catalog/client/internal/util/Base64.h"

#include "manager/catalog/client/CatalogError.h"
#include "manager/catalog/client/CatalogFailure.h"

#include "define.h"

namespace plane::catalog::internal
{
    namespace
    {
        // 单字符 -> 6bit 值; 非法字符返回 -1
        [[nodiscard]] int base64Value(char character) noexcept
        {
            if (character >= 'A' && character <= 'Z')
            {
                return character - 'A';
            }
            if (character >= 'a' && character <= 'z')
            {
                return character - 'a' + 26;
            }
            if (character >= '0' && character <= '9')
            {
                return character - '0' + 52;
            }
            if (character == '+')
            {
                return 62;
            }
            if (character == '/')
            {
                return 63;
            }
            return -1;
        }

        [[nodiscard]] Result<::std::vector<::std::uint8_t>> protocol(const char* message)
        {
            return ::std::unexpected(makeFailure(CatalogError::PROTOCOL_ERROR, message));
        }
    } // namespace

    Result<::std::vector<::std::uint8_t>> decodeBase64(const ::std::string& text)
    {
        if (text.size() % 4 != 0)
        {
            return protocol("invalid base64 length");
        }

        ::std::vector<::std::uint8_t> out {};
        out.reserve((text.size() / 4) * 3);

        for (::std::size_t index { 0 }; index < text.size(); index += 4)
        {
            int  values[4] {};
            int  padding { 0 };
            bool saw_padding { false };
            for (::std::size_t slot { 0 }; slot < 4; ++slot)
            {
                const char character { text[index + slot] };
                if (character == '=')
                {
                    // 填充必须连续到本组末尾, 且本组必须是整串最后一组
                    if (index + 4 != text.size())
                    {
                        return protocol("invalid base64 payload");
                    }
                    saw_padding = true;
                    ++padding;
                    values[slot] = 0;
                    continue;
                }
                if (saw_padding)
                {
                    return protocol("invalid base64 payload");
                }
                const int value { base64Value(character) };
                if (value < 0)
                {
                    return protocol("invalid base64 payload");
                }
                values[slot] = value;
            }
            if (padding > 2)
            {
                return protocol("invalid base64 payload");
            }

            const ::std::uint32_t chunk { (static_cast<::std::uint32_t>(values[0]) << 18u) | (static_cast<::std::uint32_t>(values[1]) << 12u) |
                                          (static_cast<::std::uint32_t>(values[2]) << 6u) | static_cast<::std::uint32_t>(values[3]) };
            out.push_back(static_cast<::std::uint8_t>((chunk >> 16u) & 0Xffu));
            if (padding < 2)
            {
                out.push_back(static_cast<::std::uint8_t>((chunk >> 8u) & 0Xffu));
            }
            if (padding < 1)
            {
                out.push_back(static_cast<::std::uint8_t>(chunk & 0Xffu));
            }
        }
        return out;
    }
} // namespace plane::catalog::internal

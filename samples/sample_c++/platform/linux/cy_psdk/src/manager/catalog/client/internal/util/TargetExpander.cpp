// cy_psdk/manager/catalog/client/internal/util/TargetExpander.cpp

#include "manager/catalog/client/internal/util/TargetExpander.h"

#include <fmt/format.h>
#include <system_error>
#include <charconv>
#include <cstdint>

#include "manager/catalog/client/CatalogError.h"
#include "manager/catalog/client/CatalogFailure.h"
#include "manager/catalog/client/internal/util/TextUtil.h"

#include "define.h"

namespace plane::catalog::internal
{
    namespace
    {
        [[nodiscard]] ::std::string ipv4ToString(::std::uint64_t ip)
        {
            return ::fmt::format("{}.{}.{}.{}", (ip >> 24u) & 0Xffu, (ip >> 16u) & 0Xffu, (ip >> 8u) & 0Xffu, ip & 0Xffu);
        }

        // 严格十进制解析 (对齐 java Integer.parseInt): 必须完整消费且为纯数字
        [[nodiscard]] int parseOctet(::std::string_view value)
        {
            int         octet { 0 };
            const char* first { value.data() };
            const char* last { first + value.size() };
            const auto  parsed { ::std::from_chars(first, last, octet) };
            if (parsed.ec != ::std::errc {} || parsed.ptr != last || octet < 0 || octet > 255)
            {
                throw ::std::invalid_argument { "非法 IP" };
            }
            return octet;
        }

        [[nodiscard]] ::std::uint64_t parseIpv4(::std::string_view value)
        {
            ::std::uint64_t result { 0 };
            ::std::size_t   start { 0 };
            int             part { 0 };
            for (::std::size_t index { 0 }; index <= value.size(); ++index)
            {
                if (index == value.size() || value[index] == '.')
                {
                    if (part >= 4)
                    {
                        throw ::std::invalid_argument { "非法 IP" };
                    }
                    result = (result << 8u) | static_cast<::std::uint64_t>(parseOctet(value.substr(start, index - start)));
                    start  = index + 1;
                    ++part;
                }
            }
            if (part != 4)
            {
                throw ::std::invalid_argument { "非法 IP" };
            }
            return result;
        }

        // 展开超限异常 (内部标记)
        struct TooManyTargets
        {};

        // 展开一个规格到 result; 返回 false 表示超出上限
        [[nodiscard]] bool expandSpec(::std::string_view spec, int max_count, ::std::vector<::std::string>& result)
        {
            auto add = [&result, max_count](::std::uint64_t ip) -> bool
            {
                if (static_cast<int>(result.size()) >= max_count)
                {
                    return false;
                }
                result.push_back(ipv4ToString(ip));
                return true;
            };

            if (spec.find('*') != ::std::string_view::npos)
            {
                const ::std::size_t wildcard { spec.find('*') };
                if (wildcard != spec.size() - 1 || wildcard == 0 || spec[wildcard - 1] != '.' ||
                    spec.find('*', wildcard + 1) != ::std::string_view::npos)
                {
                    throw ::std::invalid_argument { "非法通配目标" };
                }
                const ::std::string   prefix { ::std::string { spec.substr(0, wildcard) } + "0" };
                const ::std::uint64_t network { parseIpv4(prefix) & 0Xff'ff'ff'00ull };
                for (::std::uint64_t ip { network + 1 }; ip <= network + 254; ++ip)
                {
                    if (!add(ip))
                    {
                        throw TooManyTargets {};
                    }
                }
                return true;
            }

            if (spec.find('/') != ::std::string_view::npos)
            {
                const ::std::size_t      slash { spec.find('/') };
                const ::std::uint64_t    base { parseIpv4(spec.substr(0, slash)) };
                const ::std::string_view prefix_field { spec.substr(slash + 1) };
                int                      prefix { 0 };
                const auto prefix_parsed { ::std::from_chars(prefix_field.data(), prefix_field.data() + prefix_field.size(), prefix) };
                if (prefix_parsed.ec != ::std::errc {} || prefix_parsed.ptr != prefix_field.data() + prefix_field.size() || prefix < 0 ||
                    prefix > 32)
                {
                    throw ::std::invalid_argument { "非法 CIDR 前缀" };
                }
                const ::std::uint64_t mask { prefix == 0 ? 0ull : (0Xff'ff'ff'ffull << static_cast<unsigned>(32 - prefix)) & 0Xff'ff'ff'ffull };
                const ::std::uint64_t network { base & mask };
                const ::std::uint64_t broadcast { network | (~mask & 0Xff'ff'ff'ffull) };
                const ::std::uint64_t first { prefix >= 31 ? network : network + 1 };
                const ::std::uint64_t last { prefix >= 31 ? broadcast : broadcast - 1 };
                for (::std::uint64_t ip { first }; ip <= last; ++ip)
                {
                    if (!add(ip))
                    {
                        throw TooManyTargets {};
                    }
                }
                return true;
            }

            if (spec.find('-') != ::std::string_view::npos)
            {
                const ::std::size_t   dash { spec.find('-') };
                const ::std::uint64_t start { parseIpv4(spec.substr(0, dash)) };
                const ::std::string   right { spec.substr(dash + 1) };
                const ::std::uint64_t end { right.find('.') != ::std::string::npos
                                                ? parseIpv4(right)
                                                : (start & 0Xff'ff'ff'00ull) | static_cast<::std::uint64_t>(parseOctet(right)) };
                if (end < start)
                {
                    throw ::std::invalid_argument { "范围终点小于起点" };
                }
                for (::std::uint64_t ip { start }; ip <= end; ++ip)
                {
                    if (!add(ip))
                    {
                        throw TooManyTargets {};
                    }
                }
                return true;
            }

            if (!add(parseIpv4(spec)))
            {
                throw TooManyTargets {};
            }
            return true;
        }
    } // namespace

    Result<::std::vector<::std::string>> expandTargets(const ::std::vector<::std::string>& specs, int max_count)
    {
        ::std::vector<::std::string> result {};
        for (const auto& raw : specs)
        {
            const ::std::string clean { trimAsciiWhitespaceCopy(raw) };
            if (clean.empty())
            {
                continue;
            }
            try
            {
                (void)expandSpec(clean, max_count, result);
            }
            catch (const TooManyTargets&)
            {
                return ::std::unexpected(makeFailure(CatalogError::INVALID_ARGUMENT, ::fmt::format("展开后的目标数量超过上限 {}", max_count)));
            }
            catch (const ::std::exception& ex)
            {
                // 与 java 一致: 消息带目标原文后缀
                return ::std::unexpected(makeFailure(CatalogError::INVALID_ARGUMENT, ::std::string { ex.what() } + ": " + clean));
            }
        }

        if (result.empty())
        {
            return ::std::unexpected(makeFailure(CatalogError::INVALID_ARGUMENT, "目标列表为空"));
        }
        return result;
    }
} // namespace plane::catalog::internal

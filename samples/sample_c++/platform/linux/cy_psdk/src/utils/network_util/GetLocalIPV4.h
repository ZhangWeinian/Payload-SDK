// cy_psdk/utils/network_util/GetLocalIPV4.h

#pragma once

#include "utils/log_util/Logger.h"

#include <gsl/gsl>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <string_view>
#include <system_error>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <ifaddrs.h>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "define.h"

namespace plane::utils
{
    class Get_local_ipv4_fun_: private Not_quite_object_
    {
    public:
        using Not_quite_object_::Not_quite_object_;

        [[nodiscard]] ::std::optional<::std::string> operator()(void) noexcept
        {
            using namespace ::std::literals;
            ::std::lock_guard<::std::mutex> lock(this->cache_mutex_);
            auto                            now { ::std::chrono::steady_clock::now() };

            if (this->cached_ip_ && (now - this->cached_ip_->timestamp) < this->CACHE_DURATION)
            {
                LOG_TRACE("使用缓存的 IP 地址: {}", this->cached_ip_->ip);
                return this->cached_ip_->ip;
            }

            LOG_DEBUG("缓存失效或不存在，正在重新扫描网络接口");
            auto result { this->getDeviceIpv4AddressImpl() };
            if (result)
            {
                this->cached_ip_ = CachedResult { .ip = *result, .timestamp = now };
            }
            else
            {
                this->cached_ip_.reset();
            }
            return result;
        }

    private:
        struct CachedResult
        {
            ::std::string                           ip {};
            ::std::chrono::steady_clock::time_point timestamp {};
        };

        ::std::optional<CachedResult> cached_ip_ {};
        ::std::mutex                  cache_mutex_ {};
        constexpr static auto         CACHE_DURATION { ::std::chrono::minutes(5) };

        bool                          isSiteLocalAddress(::std::string_view ip) const noexcept
        {
            using namespace ::std::literals;
            if (ip.starts_with("192.168."sv) || ip.starts_with("10."sv))
            {
                return true;
            }

            if (ip.starts_with("172."sv))
            {
                if (auto dot_pos { ip.find('.', 4) }; dot_pos != ::std::string_view::npos)
                {
                    ::std::string_view segment { ip.substr(4, dot_pos - 4) };
                    int                value { 0 };

                    for (char c : segment)
                    {
                        if (c < '0' || c > '9')
                        {
                            return false;
                        }
                        value = value * 10 + (c - '0');
                    }
                    return value >= 16 && value <= 31;
                }
            }
            return false;
        }

        bool isHighPriorityInterface(::std::string_view name) const noexcept
        {
            using namespace ::std::literals;
            constexpr static auto WLAN_PREFIXES = ::std::array { "wlan"sv, "wlp"sv, "wlo"sv };
            constexpr static auto ETH_PREFIXES  = ::std::array { "eth"sv, "en"sv, "eno"sv, "ens"sv, "enp"sv };

            for (auto prefix : WLAN_PREFIXES)
            {
                if (name.starts_with(prefix))
                {
                    return true;
                }
            }
            for (auto prefix : ETH_PREFIXES)
            {
                if (name.starts_with(prefix))
                {
                    return true;
                }
            }
            return false;
        }

        ::std::optional<::std::string> getDeviceIpv4AddressImpl(void) const noexcept
        {
            using namespace ::std::literals;
            struct ifaddrs* ifaddr { nullptr };
            if (::getifaddrs(&ifaddr) == -1)
            {
                LOG_WARN("getifaddrs() 失败: {}", ::std::system_error(errno, ::std::system_category()).what());
                return ::std::nullopt;
            }

            auto ifaddr_guard = ::gsl::finally(
                [&]
                {
                    if (ifaddr)
                    {
                        ::freeifaddrs(ifaddr);
                    }
                }
            );

            ::std::vector<::std::pair<::std::string, ::std::string>> addresses {};
            for (auto* ifa { ifaddr }; ifa != nullptr; ifa = ifa->ifa_next)
            {
                if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET || !ifa->ifa_name)
                {
                    continue;
                }

                ::std::string_view name_sv(ifa->ifa_name);
                if (name_sv == "lo"sv)
                {
                    continue;
                }

                auto*                               addr { reinterpret_cast<sockaddr_in*>(ifa->ifa_addr) };
                ::std::array<char, INET_ADDRSTRLEN> ip_buffer {};
                if (::inet_ntop(AF_INET, &addr->sin_addr, ip_buffer.data(), ip_buffer.size()) == nullptr)
                {
                    continue;
                }

                ::std::string_view ip_sv(ip_buffer.data());
                if (ip_sv.empty() || ip_sv == "0.0.0.0"sv)
                {
                    continue;
                }

                addresses.emplace_back(::std::string(name_sv), ::std::string(ip_sv));
            }

            if (addresses.empty())
            {
                LOG_WARN("未找到任何有效的 IPv4 地址");
                return ::std::nullopt;
            }

            auto high_priority_it = ::std::find_if(
                addresses.begin(),
                addresses.end(),
                [&](const auto& pair)
                {
                    return this->isHighPriorityInterface(pair.first);
                }
            );

            if (high_priority_it != addresses.end())
            {
                LOG_INFO("已优先选择 Wi-Fi/以太网 IP 地址: {} ({})", high_priority_it->second, high_priority_it->first);
                return high_priority_it->second;
            }

            auto site_local_it = ::std::find_if(
                addresses.begin(),
                addresses.end(),
                [&](const auto& pair)
                {
                    return this->isSiteLocalAddress(pair.second);
                }
            );

            if (site_local_it != addresses.end())
            {
                LOG_INFO("未找到 Wi-Fi/以太网 IP, 已选择局域网 IP: {} ({})", site_local_it->second, site_local_it->first);
                return site_local_it->second;
            }

            const auto& [name, ip] { addresses.front() };
            LOG_INFO("未找到首选 IP, 将使用第一个有效地址作为备用: {} ({})", ip, name);
            return ip;
        }
    };

    inline Get_local_ipv4_fun_ getLocalIPV4 { Not_quite_object_::Construct_tag_ {} };
} // namespace plane::utils

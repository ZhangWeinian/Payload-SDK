// manifold2/utils/NetworkUtils/NetworkUtils.cpp

#include "utils/Logger/Logger.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <string_view>
#include <system_error>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <format>
#include <ifaddrs.h>
#include <memory>
#include <mutex>
#include <ranges>
#include <string>
#include <vector>

#include "utils/NetworkUtils/NetworkUtils.h"

namespace plane::utils
{
	namespace
	{
		using namespace _STD string_view_literals;

		constexpr auto		 LOOPBACK_INTERFACE { "lo"sv };
		constexpr auto		 INVALID_IP { "0.0.0.0"sv };
		constexpr auto WLAN_PREFIXES = _STD array { "wlan"sv, "wlp"sv, "wlo"sv };
		constexpr auto ETH_PREFIXES	 = _STD array { "eth"sv, "en"sv, "eno"sv, "ens"sv, "enp"sv };

		struct CachedResult
		{
			_STD string ip {};
			_STD_CHRONO steady_clock::time_point timestamp {};
		};

		static _STD optional<CachedResult>	 cachedIp {};
		static _STD mutex					 cacheMutex {};
		constexpr static _STD_CHRONO minutes CACHE_DURATION { 5 };

		bool								 isSiteLocalAddress(_STD string_view ip) noexcept
		{
			if (ip.starts_with("192.168."sv) || ip.starts_with("10."sv))
			{
				return true;
			}

			if (ip.starts_with("172."sv))
			{
				if (auto dot_pos { ip.find('.', 4) }; dot_pos != _STD string_view::npos)
				{
					_STD string_view segment { ip.substr(4, dot_pos - 4) };
					int				 value { 0 };
					for (char c : segment)
					{
						if (c < '0' || c > '9')
						{
							break;
						}
						value = value * 10 + (c - '0');
					}
					return value >= 16 && value <= 31;
				}
			}

			return false;
		}

		bool isHighPriorityInterface(_STD string_view name) noexcept
		{
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
	} // namespace

	_STD optional<_STD string> NetworkUtils::getDeviceIpv4AddressImpl(void) noexcept
	{
		struct ifaddrs* ifaddr {};
		if (_CSTD getifaddrs(&ifaddr) == -1)
		{
			LOG_WARN("getifaddrs() 失败: {}", _STD system_error(errno, _STD system_category()).what());
			return _STD nullopt;
		}

		auto ifaddr_guard { _STD unique_ptr<struct ifaddrs, decltype(&freeifaddrs)>(ifaddr, freeifaddrs) };
		_STD vector<_STD pair<_STD string, _STD string>> addresses {};

		for (auto* ifa { ifaddr }; ifa != nullptr; ifa = ifa->ifa_next)
		{
			if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
			{
				continue;
			}

			auto* addr { reinterpret_cast<sockaddr_in*>(ifa->ifa_addr) };
			_STD array<char, INET_ADDRSTRLEN> ip {};
			_CSTD							  inet_ntop(AF_INET, &addr->sin_addr, ip.data(), ip.size());

			_STD string						  name_str { ifa->ifa_name };
			_STD string						  ip_str { ip.data() };

			if (name_str == LOOPBACK_INTERFACE || ip_str.empty() || ip_str == INVALID_IP)
			{
				continue;
			}

			addresses.emplace_back(_STD move(name_str), _STD move(ip_str));
		}

		if (addresses.empty())
		{
			LOG_WARN("未找到任何有效的 IPv4 地址。");
			return _STD nullopt;
		}

		_STD string log_message { "扫描到的有效 IP 地址:\n" };
		int			index { 0 };
		for (const auto& [name, ip] : addresses)
		{
			log_message += _STD format("  {}. 接口名: {}, IP: {}\n", ++index, name, ip);
		}
		LOG_INFO("{}", log_message);

		auto highPriorityIt = _STD find_if(addresses.begin(),
										   addresses.end(),
										   [](const auto& name_ip)
										   {
											   const auto& [name, ip] = name_ip;
											   return isHighPriorityInterface(name);
										   });

		if (highPriorityIt != addresses.end())
		{
			const auto& [name, ip] = *highPriorityIt;
			LOG_INFO("已优先选择 Wi-Fi 或以太网 IP 地址: {} ({})", ip, name);
			return ip;
		}

		auto siteLocalIt = _STD find_if(addresses.begin(),
										addresses.end(),
										[](const auto& name_ip)
										{
											const auto& [name, ip] = name_ip;
											return isSiteLocalAddress(ip);
										});

		if (siteLocalIt != addresses.end())
		{
			const auto& [name, ip] = *siteLocalIt;
			LOG_INFO("未找到 Wi-Fi 或以太网 IP, 已选择局域网 IP: {} ({})", ip, name);
			return ip;
		}

		const auto& [name, ip] = addresses.front();
		LOG_INFO("未找到首选的局域网 IP, 将使用找到的第一个有效 IPv4 地址作为备用: {} ({})", ip, name);
		return ip;
	}

	_STD optional<_STD string> NetworkUtils::getDeviceIpv4Address(void) noexcept
	{
		_STD lock_guard<_STD mutex> lock(cacheMutex);
		auto						now { _STD_CHRONO steady_clock::now() };

		if (cachedIp && (now - cachedIp->timestamp) < CACHE_DURATION)
		{
			LOG_DEBUG("使用缓存的 IP 地址: {}", cachedIp->ip);
			return cachedIp->ip;
		}

		auto result { getDeviceIpv4AddressImpl() };
		if (result)
		{
			cachedIp = CachedResult { *result, now };
		}
		else
		{
			cachedIp.reset();
		}

		return result;
	}
} // namespace plane::utils

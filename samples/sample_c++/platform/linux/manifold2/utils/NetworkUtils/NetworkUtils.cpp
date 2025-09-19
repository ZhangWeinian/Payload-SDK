#include "utils/NetworkUtils/NetworkUtils.h"

#include "utils/Logger/Logger.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <ifaddrs.h>

#include <string_view>
#include <system_error>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <format>
#include <memory>
#include <mutex>
#include <ranges>
#include <string>
#include <vector>

namespace plane::utils
{
	namespace
	{
		using namespace std::string_view_literals;

		constexpr auto LOOPBACK_INTERFACE { "lo"sv };
		constexpr auto WLAN_PREFIXES = std::array { "wlan"sv, "wlp"sv, "wlo"sv };
		constexpr auto ETH_PREFIXES	 = std::array { "eth"sv, "en"sv, "eno"sv, "ens"sv, "enp"sv };
		constexpr auto INVALID_IP { "0.0.0.0"sv };

		struct CachedResult
		{
			std::string							  ip {};
			std::chrono::steady_clock::time_point timestamp {};
		};

		static std::optional<CachedResult>	  cachedIp {};
		static std::mutex					  cacheMutex {};
		constexpr static std::chrono::minutes CACHE_DURATION { 5 };

		bool								  isSiteLocalAddress(std::string_view ip) noexcept
		{
			if (ip.starts_with("192.168."sv) || ip.starts_with("10."sv))
			{
				return true;
			}

			if (ip.starts_with("172."sv))
			{
				if (auto dot_pos { ip.find('.', 4) }; dot_pos != std::string_view::npos)
				{
					std::string_view segment { ip.substr(4, dot_pos - 4) };
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

		bool isHighPriorityInterface(std::string_view name) noexcept
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

	std::optional<std::string> NetworkUtils::getDeviceIpv4AddressImpl(void) noexcept
	{
		struct ifaddrs* ifaddr {};
		if (getifaddrs(&ifaddr) == -1)
		{
			LOG_WARN("getifaddrs() 失败: {}", std::system_error(errno, std::system_category()).what());
			return std::nullopt;
		}

		auto ifaddr_guard { std::unique_ptr<struct ifaddrs, decltype(&freeifaddrs)>(ifaddr, freeifaddrs) };
		std::vector<std::pair<std::string, std::string>> addresses {};

		for (struct ifaddrs* ifa { ifaddr }; ifa != nullptr; ifa = ifa->ifa_next)
		{
			if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
			{
				continue;
			}

			struct sockaddr_in* addr { reinterpret_cast<struct sockaddr_in*>(ifa->ifa_addr) };
			char				ip[INET_ADDRSTRLEN] {};
			inet_ntop(AF_INET, &addr->sin_addr, ip, INET_ADDRSTRLEN);

			std::string name_str(ifa->ifa_name);
			std::string ip_str(ip);

			if (name_str == LOOPBACK_INTERFACE || ip_str.empty() || ip_str == INVALID_IP)
			{
				continue;
			}

			addresses.emplace_back(std::move(name_str), std::move(ip_str));
		}

		if (addresses.empty())
		{
			LOG_WARN("未找到任何有效的 IPv4 地址。");
			return std::nullopt;
		}

		std::string log_message { "扫描到的有效 IP 地址:\n" };
		int			index { 0 };
		for (const auto& [name, ip] : addresses)
		{
			log_message += std::format("  {}. 接口名: {}, IP: {}\n", ++index, name, ip);
		}
		LOG_INFO("{}", log_message);

		auto highPriorityIt = std::find_if(addresses.begin(),
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

		auto siteLocalIt = std::find_if(addresses.begin(),
										addresses.end(),
										[](const auto& name_ip)
										{
											const auto& [name, ip] = name_ip;
											return isSiteLocalAddress(ip);
										});

		if (siteLocalIt != addresses.end())
		{
			const auto& [name, ip] = *siteLocalIt;
			LOG_WARN("未找到 Wi-Fi 或以太网 IP，已选择局域网 IP: {} ({})", ip, name);
			return ip;
		}

		const auto& [name, ip] = addresses.front();
		LOG_WARN("未找到首选的局域网 IP，将使用找到的第一个有效 IPv4 地址作为备用: {} ({})", ip, name);
		return ip;
	}

	std::optional<std::string> NetworkUtils::getDeviceIpv4Address(void) noexcept
	{
		std::lock_guard<std::mutex> lock(cacheMutex);
		auto						now { std::chrono::steady_clock::now() };

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

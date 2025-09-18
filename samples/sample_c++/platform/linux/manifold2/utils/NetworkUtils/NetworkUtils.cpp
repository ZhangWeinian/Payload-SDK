#include "utils/Logger/Logger.h"
#include "utils/NetworkUtils/NetworkUtils.h"

#include "hv/ifconfig.h"
#include "range/v3/all.hpp"
#include "range/v3/view.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace plane::utils
{
	namespace
	{
		bool isSiteLocalAddress(const std::string& ip) noexcept
		{
			return (ip.rfind("192.168.", 0) == 0) || (ip.rfind("10.", 0) == 0) ||
				   (ip.rfind("172.", 0) == 0 && std::stoi(ip.substr(4, ip.find('.') - 4)) >= 16 &&
					std::stoi(ip.substr(4, ip.find('.') - 4)) <= 31);
		}
	} // namespace

	std::optional<std::string> NetworkUtils::getDeviceIpv4Address(void) noexcept
	{
		struct FoundAddress
		{
			std::string interfaceName {};
			std::string ipAddress {};
		};

		std::vector<ifconfig_t> interfaces(20);
		int						num_interfaces { ifconfig(interfaces) };

		if (num_interfaces <= 0)
		{
			LOG_WARN("未能扫描到任何网络接口。");
			return std::nullopt;
		}

		auto allAddresses = ranges::views::iota(0, num_interfaces) |
							ranges::views::transform(
								[&](int i)
								{
									return interfaces[i];
								}) |
							ranges::views::filter(
								[](const ifconfig_t& iface)
								{
									return strcmp(iface.name, "lo") != 0 && strlen(iface.ip) > 0 && strcmp(iface.ip, "0.0.0.0") != 0;
								}) |
							ranges::views::transform(
								[](const ifconfig_t& iface)
								{
									return FoundAddress { iface.name, iface.ip };
								}) |
							ranges::to<std::vector>();

		if (allAddresses.empty())
		{
			LOG_WARN("未找到任何有效的 IPv4 地址。");
			return std::nullopt;
		}

		std::string log_message = "扫描到的有效 IP 地址:\n";
		for (const auto& [i, addr] : ranges::views::enumerate(allAddresses))
		{
			log_message += fmt::format("  {}. 接口名: {}, IP: {}\n", i + 1, addr.interfaceName, addr.ipAddress);
		}
		LOG_INFO("{}", log_message);

		auto highPriorityResult = allAddresses |
								  ranges::views::filter(
									  [](const FoundAddress& addr)
									  {
										  return (addr.interfaceName.rfind("wlan", 0) == 0) || (addr.interfaceName.rfind("eth", 0) == 0) ||
												 (addr.interfaceName.rfind("enp", 0) == 0);
									  }) |
								  ranges::views::take(1);

		if (!highPriorityResult.empty())
		{
			LOG_INFO("已优先选择 Wi-Fi 或以太网 IP 地址。");
			return highPriorityResult.front().ipAddress;
		}

		auto siteLocalResult = allAddresses |
							   ranges::views::filter(
								   [](const FoundAddress& addr)
								   {
									   return isSiteLocalAddress(addr.ipAddress);
								   }) |
							   ranges::views::take(1);

		if (!siteLocalResult.empty())
		{
			LOG_WARN("未找到 Wi-Fi 或以太网 IP，已选择第一个找到的局域网 IP。");
			return siteLocalResult.front().ipAddress;
		}

		LOG_WARN("未找到首选的局域网 IP，将使用找到的第一个有效 IPv4 地址作为备用。");
		return allAddresses.front().ipAddress;
	}
} // namespace plane::utils

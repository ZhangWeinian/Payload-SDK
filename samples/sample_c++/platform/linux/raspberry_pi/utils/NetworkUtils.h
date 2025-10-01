// raspberry_pi/utils/NetworkUtils.h

#pragma once

#include "utils/Logger.h"

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
	class NetworkUtils
	{
	public:
		static NetworkUtils& getInstance(void) noexcept
		{
			static NetworkUtils instance {};
			return instance;
		}

		_NODISCARD _STD optional<_STD string> getDeviceIpv4Address(void) noexcept
		{
			using namespace _STD literals;
			_STD lock_guard<_STD mutex> lock(this->cache_mutex_);
			auto						now { _STD_CHRONO steady_clock::now() };

			if (this->cached_ip_ && (now - this->cached_ip_->timestamp) < this->CACHE_DURATION)
			{
				LOG_TRACE("使用缓存的 IP 地址: {}", this->cached_ip_->ip);
				return this->cached_ip_->ip;
			}

			LOG_DEBUG("缓存失效或不存在，正在重新扫描网络接口...");
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
			_STD string ip {};
			_STD_CHRONO steady_clock::time_point timestamp {};
		};

		explicit NetworkUtils(void) noexcept		 = default;
		~NetworkUtils(void) noexcept				 = default;
		NetworkUtils(const NetworkUtils&)			 = delete;
		NetworkUtils& operator=(const NetworkUtils&) = delete;

		_STD optional<CachedResult> cached_ip_ {};
		_STD mutex					cache_mutex_ {};
		constexpr static auto		CACHE_DURATION { _STD_CHRONO minutes(5) };

		bool						isSiteLocalAddress(_STD string_view ip) const noexcept
		{
			using namespace _STD literals;
			if (ip.starts_with("192.168."sv) || ip.starts_with("10."sv))
			{
				return true;
			}

			if (ip.starts_with("172."sv))
			{
				if (auto dot_pos = ip.find('.', 4); dot_pos != _STD string_view::npos)
				{
					_STD string_view segment { ip.substr(4, dot_pos - 4) };
					int				 value { 0 };

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

		bool isHighPriorityInterface(_STD string_view name) const noexcept
		{
			using namespace _STD  literals;
			constexpr static auto WLAN_PREFIXES = _STD array { "wlan"sv, "wlp"sv, "wlo"sv };
			constexpr static auto ETH_PREFIXES	= _STD array { "eth"sv, "en"sv, "eno"sv, "ens"sv, "enp"sv };

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

		_STD optional<_STD string> getDeviceIpv4AddressImpl(void) const noexcept
		{
			using namespace _STD literals;

			struct ifaddrs*		 ifaddr { nullptr };
			if (_CSTD getifaddrs(&ifaddr) == -1)
			{
				LOG_WARN("getifaddrs() 失败: {}", _STD system_error(errno, _STD system_category()).what());
				return _STD nullopt;
			}

			auto ifaddr_guard = _GSL finally(
				[&]
				{
					if (ifaddr)
					{
						_CSTD freeifaddrs(ifaddr);
					}
				});

			_STD vector<_STD pair<_STD string, _STD string>> addresses {};

			for (auto* ifa { ifaddr }; ifa != nullptr; ifa = ifa->ifa_next)
			{
				if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET || !ifa->ifa_name)
				{
					continue;
				}

				_STD string_view name_sv(ifa->ifa_name);
				if (name_sv == "lo"sv)
				{
					continue;
				}

				auto* addr { reinterpret_cast<sockaddr_in*>(ifa->ifa_addr) };
				_STD array<char, INET_ADDRSTRLEN> ip_buffer {};
				if (_CSTD inet_ntop(AF_INET, &addr->sin_addr, ip_buffer.data(), ip_buffer.size()) == nullptr)
				{
					continue;
				}

				_STD string_view ip_sv(ip_buffer.data());
				if (ip_sv.empty() || ip_sv == "0.0.0.0"sv)
				{
					continue;
				}

				addresses.emplace_back(_STD string(name_sv), _STD string(ip_sv));
			}

			if (addresses.empty())
			{
				LOG_WARN("未找到任何有效的 IPv4 地址。");
				return _STD nullopt;
			}

			auto highPriorityIt = _STD find_if(addresses.begin(),
											   addresses.end(),
											   [&](const auto& pair)
											   {
												   return this->isHighPriorityInterface(pair.first);
											   });

			if (highPriorityIt != addresses.end())
			{
				LOG_INFO("已优先选择 Wi-Fi/以太网 IP 地址: {} ({})", highPriorityIt->second, highPriorityIt->first);
				return highPriorityIt->second;
			}

			auto siteLocalIt = _STD find_if(addresses.begin(),
											addresses.end(),
											[&](const auto& pair)
											{
												return this->isSiteLocalAddress(pair.second);
											});

			if (siteLocalIt != addresses.end())
			{
				LOG_INFO("未找到 Wi-Fi/以太网 IP, 已选择局域网 IP: {} ({})", siteLocalIt->second, siteLocalIt->first);
				return siteLocalIt->second;
			}

			const auto& [name, ip] { addresses.front() };
			LOG_INFO("未找到首选 IP, 将使用第一个有效地址作为备用: {} ({})", ip, name);
			return ip;
		}
	};
} // namespace plane::utils

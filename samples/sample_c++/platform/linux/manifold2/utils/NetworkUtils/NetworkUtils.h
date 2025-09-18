#pragma once

#include <optional>
#include <string>

namespace plane::utils
{
	class NetworkUtils
	{
	public:
		static std::optional<std::string> getDeviceIpv4Address(void) noexcept;

		NetworkUtils(void) noexcept	 = delete;
		~NetworkUtils(void) noexcept = delete;
	};
} // namespace plane::utils

#pragma once

#include <optional>
#include <string>

namespace plane::utils
{
#ifndef _NODISCARD
	#define _NODISCARD [[nodiscard]]
#endif

	class NetworkUtils
	{
	public:
		_NODISCARD static std::optional<std::string> getDeviceIpv4Address(void) noexcept;

		NetworkUtils(void) noexcept	 = delete;
		~NetworkUtils(void) noexcept = delete;

	private:
		static std::optional<std::string> getDeviceIpv4AddressImpl(void) noexcept;
	};
} // namespace plane::utils

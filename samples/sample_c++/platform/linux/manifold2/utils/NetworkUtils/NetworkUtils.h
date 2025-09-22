#pragma once

#include "define.h"

#include <optional>
#include <string>

namespace plane::utils
{
	class NetworkUtils
	{
	public:
		_NODISCARD static _STD optional<_STD string> getDeviceIpv4Address(void) noexcept;

		NetworkUtils(void) noexcept	 = delete;
		~NetworkUtils(void) noexcept = delete;

	private:
		static _STD optional<_STD string> getDeviceIpv4AddressImpl(void) noexcept;
	};
} // namespace plane::utils

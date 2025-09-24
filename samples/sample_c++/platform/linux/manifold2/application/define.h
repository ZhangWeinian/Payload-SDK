// manifold2/application/define.h

#pragma once

#include <version>

#ifndef _NODISCARD
	#define _NODISCARD [[nodiscard]]
#endif

#ifndef _MAYBE_UNUSED
	#define _MAYBE_UNUSED [[maybe_unused]]
#endif

#ifndef _STD
	#define _STD ::std::
#endif

#ifndef _CSTD
	#define _CSTD ::
#endif

#if __has_include(<chrono>) && !defined(_STD_CHRONO)
	#define _STD_CHRONO ::std::chrono::
#endif

#if __has_include(<filesystem>) && !defined(_STD_FS)
	#define _STD_FS ::std::filesystem::
#endif

#if __has_include(<fmt/format.h>) && !defined(_FMT)
	#define _FMT ::fmt::
#endif

#if __has_include(<nlohmann/json.hpp>) && !defined(_NLOHMANN_JSON)
	#define _NLOHMANN_JSON ::nlohmann::
#endif

#if __has_include(<zip.h>) && !defined(_LIBZIP)
	#define _LIBZIP ::
#endif

#if __has_include(<mqtt/async_client.h>) && !defined(_MQTT)
	#define _MQTT ::mqtt::
#endif

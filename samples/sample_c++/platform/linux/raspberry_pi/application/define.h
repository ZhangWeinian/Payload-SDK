// raspberry_pi/application/define.h

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

#ifndef _DJI
	#define _DJI ::
#endif

#ifndef _UNNAMED
	#define _UNNAMED
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

#if __has_include(<spdlog/spdlog.h>) && !defined(_SPDLOG)
	#define _SPDLOG ::spdlog::
#endif

#if __has_include(<mqtt/async_client.h>) && !defined(_MQTT)
	#define _MQTT ::mqtt::
#endif

#if __has_include(<yaml-cpp/yaml.h>) && !defined(_YAML)
	#define _YAML ::YAML::
#endif

#if __has_include(<pugixml.hpp>) && !defined(_PUGI)
	#define _PUGI ::pugi::
#endif

#if __has_include(<gsl/gsl>) && !defined(_GSL)
	#define _GSL ::gsl::
#endif

#if __has_include(<ThreadPool.h>) && !defined(_THREADPOOL)
	#define _THREADPOOL ::
#endif

#if __has_include(<eventpp/eventdispatcher.h>) && !defined(_EVENTPP)
	#define _EVENTPP ::eventpp::
#endif

constexpr inline auto	MATH_PI { 3.14159265358979323846 };
constexpr inline auto	EARTH_RADIUS_M { 6'371'000.0 };
constexpr inline double RAD_TO_DEG { 180.0 / MATH_PI };

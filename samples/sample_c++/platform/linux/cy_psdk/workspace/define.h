// cy_psdk/workspace/define.h

#pragma once

#include <type_traits>
#include <functional>
#include <iterator>
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

#ifndef _THIS
	#define _THIS
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

#if __has_include(<ThreadPool/ThreadPool.h>) && !defined(_THREADPOOL)
	#define _THREADPOOL ::
#endif

#if __has_include(<eventpp/eventdispatcher.h>) && !defined(_EVENTPP)
	#define _EVENTPP ::eventpp::
#endif

#if __has_include(<CLI/CLI.hpp>) && !defined(_CLI)
	#define _CLI ::CLI::
#endif

#ifndef _DEFINED
	#define _DEFINED

	#if __has_include(<vector>) && !defined(_KMZ_DATA_TYPE)
		#define _KMZ_DATA_TYPE _STD vector<_STD uint8_t>
	#endif

	#ifndef _PTZ_CONTROL_STRATEGY_TYPE
		#define _PTZ_CONTROL_STRATEGY_TYPE int
	#endif

	#if __has_include(<string>) && !defined(_VIDEO_SOURCE_TYPE)
		#define _VIDEO_SOURCE_TYPE _STD string
	#endif

constexpr inline auto MATH_PI { 3.14159265358979323846 };
constexpr inline auto EARTH_RADIUS_M { 6'371'000.0 };
constexpr inline auto RAD_TO_DEG { 180.0 / _DEFINED MATH_PI };

/*-----------------------------------------------------------------------------------------------------*/

template<typename Predicate>
struct _add_ref_for_function
{
	Predicate& pred;

	template<class... Args>
	constexpr auto operator()(Args&&... args)
	{
		if constexpr (_STD is_member_pointer_v<Predicate>)
		{
			return _STD invoke(pred, _STD forward<Args>(args)...);
		}
		else
		{
			return pred(_STD forward<Args>(args)...);
		}
	}
};

template<typename Predicate>
_NODISCARD constexpr auto _pass_function(Predicate& pred) noexcept
{
	constexpr bool _pass_by_value = _STD conjunction_v<
		_STD bool_constant<sizeof(Predicate) <= sizeof(void*)>, // 检查 Predicate 的大小是否小于或等于指针的大小
		_STD is_trivially_copy_constructible<Predicate>,		// 检查 Predicate 是否具有平凡的复制构造函数
		_STD is_trivially_destructible<Predicate>				// 检查 Predicate 是否具有平凡的析构函数
	>;

	if constexpr (_pass_by_value)
	{
		return pred;
	}
	else
	{
		return _add_ref_for_function<Predicate> { pred };
	}
}

class __Not_quite_object
{
public:
	struct __Construct_tag
	{
		explicit __Construct_tag() = default;
	};

	constexpr explicit __Not_quite_object(__Construct_tag) noexcept {}

	__Not_quite_object()									 = delete;
	__Not_quite_object(const __Not_quite_object&)			 = delete;

	void				operator&() const					 = delete;
	__Not_quite_object& operator=(const __Not_quite_object&) = delete;

protected:
	~__Not_quite_object() = default;
};

#endif

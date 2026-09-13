// cy_psdk/workspace/define.h

#pragma once

#include <type_traits>
#include <cstdint>
#include <functional>
#include <iterator>
#include <numbers>
#include <string>
#include <utility>
#include <vector>
#include <version>

using kmz_data_type             = ::std::vector<::std::uint8_t>;
using ptz_control_strategy_type = int;
using video_source_type         = ::std::string;

constexpr inline auto MATH_PI { ::std::numbers::pi };
constexpr inline auto EARTH_RADIUS_M { 6'371'000.0 };
constexpr inline auto RAD_TO_DEG { 180.0 / ::std::numbers::pi };

template<typename Predicate>
struct add_ref_for_function_
{
    Predicate& pred;

    template<class... Args>
    constexpr auto operator()(Args&&... args)
    {
        if constexpr (::std::is_member_pointer_v<Predicate>)
        {
            return ::std::invoke(pred, ::std::forward<Args>(args)...);
        }
        else
        {
            return pred(::std::forward<Args>(args)...);
        }
    }
};

template<typename Predicate>
[[nodiscard]] constexpr auto pass_function_(Predicate& pred) noexcept
{
    constexpr bool _pass_by_value = ::std::conjunction_v<
        ::std::bool_constant<sizeof(Predicate) <= sizeof(void*)>, // 检查 Predicate 的大小是否小于或等于指针的大小
        ::std::is_trivially_copy_constructible<Predicate>,        // 检查 Predicate 是否具有平凡的复制构造函数
        ::std::is_trivially_destructible<Predicate>               // 检查 Predicate 是否具有平凡的析构函数
    >;

    if constexpr (_pass_by_value)
    {
        return pred;
    }
    else
    {
        return add_ref_for_function_<Predicate> { pred };
    }
}

class Not_quite_object_
{
public:
    struct Construct_tag_
    {
        explicit Construct_tag_() = default;
    };

    constexpr explicit Not_quite_object_(Construct_tag_) noexcept {}

    Not_quite_object_()                                    = delete;
    Not_quite_object_(const Not_quite_object_&)            = delete;

    void               operator&() const                   = delete;
    Not_quite_object_& operator=(const Not_quite_object_&) = delete;

protected:
    ~Not_quite_object_() = default;
};

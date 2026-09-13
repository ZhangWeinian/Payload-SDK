// cy_psdk/utils/DjiErrorUtils.h

#pragma once

#include <dji_error.h>
#include <dji_typedef.h>

#include <string_view>
#include <unordered_map>

#include "define.h"

namespace plane::utils
{
    class Dji_error_converter_fun_: private Not_quite_object_
    {
    public:
        using Not_quite_object_::Not_quite_object_;

        ::std::string_view operator()(::T_DjiReturnCode code) const noexcept
        {
            struct ErrorObject
            {
                ::T_DjiReturnCode code {};
                const char*       description {};
                const char*       suggestion {};
            };

            static const ErrorObject                                          errorObjects[] = { DJI_ERROR_OBJECTS };
            static const ::std::unordered_map<::T_DjiReturnCode, const char*> error_map      = []
            {
                ::std::unordered_map<::T_DjiReturnCode, const char*> m {};
                for (const auto& obj : errorObjects)
                {
                    m[obj.code] = obj.description;
                }
                return m;
            }();

            if (auto it { error_map.find(code) }; it != error_map.end())
            {
                return it->second;
            }
            else
            {
                return "UNKNOWN_ERROR_CODE";
            }
        }
    };

    constexpr inline Dji_error_converter_fun_ convertDjiError { Not_quite_object_::Construct_tag_ {} };
} // namespace plane::utils

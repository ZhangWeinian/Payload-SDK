// cy_psdk/utils/DjiErrorUtils.h

#pragma once

#include <dji_error.h>
#include <dji_typedef.h>

#include <string_view>
#include <unordered_map>

#include "define.h"

namespace plane::utils
{
	class __Dji_error_converter_fun: private __Not_quite_object
	{
	public:
		using __Not_quite_object::__Not_quite_object;

		_STD string_view operator()(_DJI T_DjiReturnCode code) const noexcept
		{
			struct ErrorObject
			{
				_DJI T_DjiReturnCode code {};
				const char*			 description {};
				const char*			 suggestion {};
			};

			static const ErrorObject errorObjects[]										 = { DJI_ERROR_OBJECTS };
			static const _STD unordered_map<_DJI T_DjiReturnCode, const char*> error_map = []
			{
				_STD unordered_map<_DJI T_DjiReturnCode, const char*> m {};
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

	constexpr inline __Dji_error_converter_fun convertDjiError { __Not_quite_object::__Construct_tag {} };
} // namespace plane::utils

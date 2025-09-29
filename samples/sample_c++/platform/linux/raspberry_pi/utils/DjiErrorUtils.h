// raspberry_pi/utils/DjiErrorUtils.h

#pragma once

#include <string_view>
#include <unordered_map>

#include <dji_error.h>
#include <dji_typedef.h>

#include "define.h"

namespace plane::utils
{
	class DjiErrorConverter
	{
	public:
		static DjiErrorConverter& getInstance(void) noexcept
		{
			static DjiErrorConverter instance {};
			return instance;
		}

		_STD string_view toString(_DJI T_DjiReturnCode code) const noexcept
		{
			if (auto it { m_errorMap.find(code) }; it != m_errorMap.end())
			{
				return it->second;
			}
			return "UNKNOWN_ERROR_CODE";
		}

	private:
		explicit DjiErrorConverter(void) noexcept
		{
			struct ErrorObject
			{
				_DJI T_DjiReturnCode code {};
				const char*			 description {};
				const char*			 suggestion {};
			};

			const ErrorObject errorObjects[] = { DJI_ERROR_OBJECTS };

			for (const auto& obj : errorObjects)
			{
				m_errorMap[obj.code] = obj.description;
			}
		}

		DjiErrorConverter(const DjiErrorConverter&) noexcept			= delete;
		DjiErrorConverter& operator=(const DjiErrorConverter&) noexcept = delete;

		_STD unordered_map<_DJI T_DjiReturnCode, const char*> m_errorMap;
	};

	inline _STD string_view djiReturnCodeToString(_DJI T_DjiReturnCode code)
	{
		return DjiErrorConverter::getInstance().toString(code);
	}
} // namespace plane::utils

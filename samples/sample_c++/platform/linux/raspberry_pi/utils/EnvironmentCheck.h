// raspberry_pi/utils/EnvironmentCheck/EnvironmentCheck.h

#pragma once

#include <string_view>
#include <cstdlib>
#include <string>

#include "define.h"

namespace plane::utils
{
	namespace
	{
		inline bool isEnvVarSetToOne(_STD string_view envVarName) noexcept
		{
			const char* envValue { _STD getenv(_STD string(envVarName).c_str()) };
			return (envValue != nullptr) && (_STD string_view(envValue) == "1");
		}
	} // namespace

	_NODISCARD inline bool isStandardProceduresEnabled(void) noexcept
	{
		// return isEnvVarSetToOne("FULL_PSDK");
		return true;
	}

	_NODISCARD inline bool isLogLevelDebug(void) noexcept
	{
		return _UNNAMED isEnvVarSetToOne("LOG_DEBUG");
	}

	_NODISCARD inline bool isSkipRC(void) noexcept
	{
		return _UNNAMED isEnvVarSetToOne("SKIP_RC");
	}

	_NODISCARD inline bool isTestKmzFile(void) noexcept
	{
		return _UNNAMED isEnvVarSetToOne("TEST_KMZ");
	}

	_NODISCARD inline bool isSaveKmz(void) noexcept
	{
		return _UNNAMED isEnvVarSetToOne("SAVE_KMZ");
	}
} // namespace plane::utils

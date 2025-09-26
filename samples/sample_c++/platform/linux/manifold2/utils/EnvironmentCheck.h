// manifold2/utils/EnvironmentCheck/EnvironmentCheck.h

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
		return isEnvVarSetToOne("FULL_PSDK");
	}

	_NODISCARD inline bool isLogLevelDebug(void) noexcept
	{
		return isEnvVarSetToOne("LOG_DEBUG");
	}

	_NODISCARD inline bool isSkipRC(void) noexcept
	{
		return isEnvVarSetToOne("SKIP_RC");
	}
} // namespace plane::utils

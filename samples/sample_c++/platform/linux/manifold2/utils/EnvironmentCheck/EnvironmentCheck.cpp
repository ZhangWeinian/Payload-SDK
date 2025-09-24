// manifold2/utils/EnvironmentCheck/EnvironmentCheck.cpp

#include <string_view>
#include <cstdlib>
#include <string>

#include "utils/EnvironmentCheck/EnvironmentCheck.h"

namespace plane::utils
{
	namespace
	{
		inline bool isEnvVarSetToOne(_STD string_view envVarName) noexcept
		{
			const char* envValue { _STD getenv(envVarName.data()) };
			return (envValue != nullptr) && (_STD string(envValue) == "1");
		}
	} // namespace

	bool isStandardProceduresEnabled(void) noexcept
	{
		return isEnvVarSetToOne("FULL_PSDK");
	}

	bool isLogLevelDebug(void) noexcept
	{
		return isEnvVarSetToOne("LOG_DEBUG");
	}
} // namespace plane::utils

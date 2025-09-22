#include "utils/EnvironmentCheck/EnvironmentCheck.h"

#include <string_view>
#include <cstdlib>
#include <string>

namespace plane::utils
{
	namespace
	{
		constexpr inline bool isEnvVarSetToOne(_STD string_view envVarName) noexcept
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

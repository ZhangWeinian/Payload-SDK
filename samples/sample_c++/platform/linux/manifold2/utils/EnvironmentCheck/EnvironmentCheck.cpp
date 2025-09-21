#include "utils/EnvironmentCheck/EnvironmentCheck.h"

#include <cstdlib>
#include <string>

namespace plane::utils
{
	bool isStandardProceduresEnabled(void) noexcept
	{
		const char* envValue { std::getenv("FULL_PSDK") };
		return (envValue != nullptr) && (std::string(envValue) == "1");
	}

	bool isLogLevelDebug(void) noexcept
	{
		const char* envValue { std::getenv("LOG_DEBUG") };
		return (envValue != nullptr) && (std::string(envValue) == "1");
	}
} // namespace plane::utils

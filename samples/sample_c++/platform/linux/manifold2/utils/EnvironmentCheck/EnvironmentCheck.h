#pragma once

namespace plane::utils
{
#ifndef _NODISCARD
	#define _NODISCARD [[nodiscard]]
#endif

	_NODISCARD bool isStandardProceduresEnabled(void) noexcept;
	_NODISCARD bool isLogLevelDebug(void) noexcept;
} // namespace plane::utils

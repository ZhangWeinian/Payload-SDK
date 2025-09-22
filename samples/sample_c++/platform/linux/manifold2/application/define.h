#pragma once

#include <version>

#ifndef _NODISCARD
	#define _NODISCARD [[nodiscard]]
#endif

#ifndef _MAYBE_UNUSED
	#define _MAYBE_UNUSED [[maybe_unused]]
#endif

#ifndef _STD
	#define _STD ::std::
#endif

#ifndef _CSTD
	#define _CSTD ::
#endif

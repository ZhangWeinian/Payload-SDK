// cy_psdk/services/PSDK/PSDKManager/PSDKManager.h

#pragma once

#include "dji_typedef.h"

#include <atomic>
#include <memory>

#include "define.h"

class Application;

namespace plane::services
{
	class PSDKManager
	{
	public:
		static PSDKManager& getInstance(void) noexcept;
		_NODISCARD bool		start(int argc, char* argv[]);
		void				stop(void);

	private:
		explicit PSDKManager(void) noexcept;
		~PSDKManager(void) noexcept;
		PSDKManager(const PSDKManager&)			   = delete;
		PSDKManager& operator=(const PSDKManager&) = delete;

		_STD unique_ptr<_DJI Application> dji_application_ {};
		_STD atomic<bool> running_ { false };
	};
} // namespace plane::services

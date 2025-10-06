// raspberry_pi/services/PSDK/PSDKManager/PSDKManager.h

#pragma once

#include "dji_typedef.h"

#include <memory>

#include "define.h"

class Application;

namespace plane::services
{
	class PSDKManager
	{
	public:
		static PSDKManager& getInstance(void) noexcept;
		_NODISCARD bool		initialize(int argc, char* argv[]);
		void				deinitialize(void);

	private:
		explicit PSDKManager(void) noexcept;
		~PSDKManager(void) noexcept;
		PSDKManager(const PSDKManager&)							   = delete;
		PSDKManager&				 operator=(const PSDKManager&) = delete;

		std::unique_ptr<Application> dji_application_ {};
	};
} // namespace plane::services

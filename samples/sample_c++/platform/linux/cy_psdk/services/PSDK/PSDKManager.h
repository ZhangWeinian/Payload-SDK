// cy_psdk/services/PSDK/PSDKManager.h

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

		// 启动 PSDK 底层服务，这是一个幂等的操作
		_NODISCARD bool start(int argc, char* argv[]);

		// 停止 PSDK 底层服务，这是一个幂等的操作
		void stop(void);

	private:
		explicit PSDKManager(void) noexcept;
		~PSDKManager(void) noexcept;
		PSDKManager(const PSDKManager&)			   = delete;
		PSDKManager& operator=(const PSDKManager&) = delete;

		_STD unique_ptr<_DJI Application> dji_application_ {};
		_STD atomic<bool> running_ { false };
	};
} // namespace plane::services

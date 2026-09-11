// cy_psdk/manager/psdk/PSDKManager.h

#pragma once

#include "dji_typedef.h"

#include <atomic>
#include <memory>

#include "define.h"

class Application;

namespace plane::manager
{
	class PSDKManager
	{
	public:
		static PSDKManager& getInstance(void) noexcept;

		// 将 PSDK 日志重定向到 spdlog (幂等); 建议在 DjiCore_Init 之前调用, 以便 CORE 初始化阶段的日志可见
		void redirectPsdkLogs(void) noexcept;

		// 启动 PSDK 底层服务，这是一个幂等的操作
		_NODISCARD bool start(int argc, char* argv[]);

		// 停止 PSDK 底层服务，这是一个幂等的操作
		void stop(void);

	private:
		explicit PSDKManager(void) noexcept;
		~PSDKManager(void) noexcept;
		PSDKManager(const PSDKManager&)			   = delete;
		PSDKManager& operator=(const PSDKManager&) = delete;

		_STD atomic<bool> running_ { false };
	};
} // namespace plane::manager

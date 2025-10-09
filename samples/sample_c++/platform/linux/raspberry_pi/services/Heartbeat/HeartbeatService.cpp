// raspberry_pi/services/Heartbeat/HeartbeatService.cpp

#include "HeartbeatService.h"
#include "services/EventManager/EventManager.h"
#include "utils/Logger.h"

namespace plane::services
{
	HeartbeatService& HeartbeatService::getInstance(void) noexcept
	{
		static HeartbeatService instance {};
		return instance;
	}

	HeartbeatService::~HeartbeatService(void) noexcept
	{
		try
		{
			this->stop();
		}
		catch (const _STD exception& e)
		{
			LOG_ERROR("心跳服务析构异常: {}", e.what());
		}
		catch (...)
		{
			LOG_ERROR("心跳服务析构发生未知异常: <non-std exception>");
		}
	}

	bool HeartbeatService::start(_STD_CHRONO milliseconds interval)
	{
		try
		{
			if (this->run_heartbeat_.exchange(true))
			{
				LOG_WARN("HeartbeatService 已经启动，请勿重复调用 start()。");
				return true;
			}

			this->heartbeat_thread_ = _STD thread(&HeartbeatService::runLoop, this, interval);
			LOG_INFO("心跳服务已启动，频率: {}ms。", interval.count());
			return true;
		}
		catch (const _STD exception& e)
		{
			LOG_ERROR("心跳服务启动失败，出现异常: {}", e.what());
			this->stop();
			return false;
		}
		catch (...)
		{
			LOG_ERROR("心跳服务启动失败，出现未知异常");
			this->stop();
			return false;
		}
	}

	void HeartbeatService::stop(void)
	{
		if (this->stopped_.exchange(true))
		{
			return;
		}

		if (!this->run_heartbeat_.exchange(false))
		{
			return;
		}

		if (this->heartbeat_thread_.joinable())
		{
			this->heartbeat_thread_.join();
			LOG_INFO("心跳服务已停止。");
		}
	}

	void HeartbeatService::runLoop(_STD_CHRONO milliseconds interval)
	{
		while (this->run_heartbeat_)
		{
			plane::services::EventManager::getInstance().publishSystemEvent(plane::services::EventManager::SystemEvent::HeartbeatTick);
			_STD this_thread::sleep_for(interval);
		}
	}
} // namespace plane::services

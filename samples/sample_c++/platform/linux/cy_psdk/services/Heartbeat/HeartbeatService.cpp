// cy_psdk/services/Heartbeat/HeartbeatService.cpp

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
		if (bool expected { false }; !this->running_.compare_exchange_strong(expected, true))
		{
			LOG_WARN("HeartbeatService 已经启动，请勿重复调用 start()。");
			return true;
		}

		try
		{
			this->heartbeat_thread_ = _STD thread(&HeartbeatService::runLoop, this, interval);
			LOG_INFO("心跳服务已启动，频率: {}ms。", interval.count());
			return true;
		}
		catch (const _STD exception& e)
		{
			LOG_ERROR("心跳服务启动失败，出现异常: {}", e.what());
			this->running_ = false;
			return false;
		}
		catch (...)
		{
			LOG_ERROR("心跳服务启动失败，出现未知异常");
			this->running_ = false;
			return false;
		}
	}

	void HeartbeatService::stop(void)
	{
		if (bool expected { true }; !this->running_.compare_exchange_strong(expected, false))
		{
			return;
		}

		if (this->heartbeat_thread_.joinable())
		{
			this->heartbeat_thread_.join();
		}
		LOG_INFO("心跳服务已停止。");
	}

	void HeartbeatService::runLoop(_STD_CHRONO milliseconds interval)
	{
		while (this->running_)
		{
			plane::services::EventManager::getInstance().publishSystemEvent(plane::services::EventManager::SystemEvent::HeartbeatTick);
			while (this->running_ && _STD_CHRONO steady_clock::now() < (_STD_CHRONO steady_clock::now() + interval))
			{
				_STD this_thread::sleep_for(_STD_CHRONO milliseconds(100));
			}
		}
	}
} // namespace plane::services

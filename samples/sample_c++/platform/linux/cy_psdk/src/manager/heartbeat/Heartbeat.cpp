// cy_psdk/manager/heartbeat/Heartbeat.cpp

#include "manager/heartbeat/Heartbeat.h"

#include "manager/event_manager/EventManager.h"
#include "utils/log_util/Logger.h"

namespace plane::manager
{
    Heartbeat& Heartbeat::getInstance(void) noexcept
    {
        static Heartbeat instance {};
        return instance;
    }

    Heartbeat::~Heartbeat(void) noexcept
    {
        try
        {
            this->stop();
        }
        catch (const ::std::exception& e)
        {
            LOG_ERROR("心跳服务析构异常: {}", e.what());
        }
        catch (...)
        {
            LOG_ERROR("心跳服务析构发生未知异常: <non-std exception>");
        }
    }

    bool Heartbeat::start(::std::chrono::milliseconds interval)
    {
        if (bool expected { false }; !this->running_.compare_exchange_strong(expected, true))
        {
            LOG_WARN("Heartbeat 已经启动，请勿重复调用 start()");
            return true;
        }

        try
        {
            this->heartbeat_thread_ = ::std::thread(&Heartbeat::runLoop, this, interval);
            const auto& frequency { 1000.0 / static_cast<double>(interval.count()) };
            LOG_INFO("心跳服务已启动，频率: {:.3f}Hz", frequency);
            return true;
        }
        catch (const ::std::exception& e)
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

    void Heartbeat::stop(void)
    {
        if (bool expected { true }; !this->running_.compare_exchange_strong(expected, false))
        {
            return;
        }

        if (this->heartbeat_thread_.joinable())
        {
            this->heartbeat_thread_.join();
        }
        LOG_INFO("心跳服务已停止");
    }

    void Heartbeat::runLoop(::std::chrono::milliseconds interval)
    {
        auto next_wakeup_time { ::std::chrono::steady_clock::now() };
        while (this->running_)
        {
            next_wakeup_time += interval;
            plane::manager::EventManager::getInstance().publishSystemEvent(plane::manager::EventManager::SystemEvent::HeartbeatTick);
            ::std::this_thread::sleep_until(next_wakeup_time);
        }
    }
} // namespace plane::manager

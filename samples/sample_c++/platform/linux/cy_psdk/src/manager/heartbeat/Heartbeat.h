// cy_psdk/manager/heartbeat/Heartbeat.h
#pragma once

#include <atomic>
#include <chrono>
#include <thread>

#include "define.h"

namespace plane::manager
{
    class Heartbeat
    {
    public:
        static Heartbeat& getInstance(void) noexcept;

        // 启动心跳服务，interval 参数指定心跳间隔，默认为 1 秒
        [[nodiscard]] bool start(::std::chrono::milliseconds interval = ::std::chrono::seconds(1));

        // 停止心跳服务
        void stop(void);

    private:
        explicit Heartbeat(void) noexcept = default;
        ~Heartbeat(void) noexcept;
        Heartbeat(const Heartbeat&)                     = delete;
        Heartbeat&          operator=(const Heartbeat&) = delete;

        void                runLoop(::std::chrono::milliseconds interval);

        ::std::thread       heartbeat_thread_ {};
        ::std::atomic<bool> running_ { false };
    };
} // namespace plane::manager

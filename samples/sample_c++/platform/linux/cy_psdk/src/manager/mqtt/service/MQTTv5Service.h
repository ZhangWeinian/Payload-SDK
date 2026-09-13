// cy_psdk/manager/mqtt/service/MQTTv5Service.h

#pragma once

#include "manager/event_manager/EventManager.h"

#include <eventpp/utilities/scopedremover.h>
#include <mqtt/async_client.h>

#include <condition_variable>
#include <string_view>
#include <atomic>
#include <cassert>
#include <deque>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <utility>

#include "define.h"

namespace plane::manager
{
    class MQTTv5Service
    {
    protected:
        struct Impl
        {
            ::std::unique_ptr<::mqtt::async_client>                 client {};
            ::std::shared_ptr<class MqttCallback>                   callback {};
            ::std::string                                           serverURI {};
            ::std::string                                           clientId {};
            int                                                     reconnectAttempts { 0 };
            ::std::chrono::steady_clock::time_point                 lastDisconnectTime {};
            ::std::chrono::steady_clock::time_point                 lastConnectTime {};
            bool                                                    manualDisconnect { false };
            ::std::deque<::std::pair<::std::string, ::std::string>> messageDeque {};
            ::std::mutex                                            dequeMutex {};
            ::std::condition_variable                               dequeCv {};
            ::std::thread                                           senderThread {};
            ::std::atomic<bool>                                     runSender { false };
            bool                                                    isDroppingMessages { false };
            ::std::chrono::steady_clock::time_point                 lastDropLogTime {};

            // ---- 维护线程 (周期自检 / 断线重连) ----
            ::std::thread                           maintainThread {};
            ::std::atomic<bool>                     runMaintain { false };
            ::std::mutex                            maintainMutex {};
            ::std::condition_variable               maintainCv {};
            ::std::mutex                            clientMutex {};     // 保护 client 指针与连接状态的交换/使用
            ::std::string                           activeUrl {};       // 最近一次连接尝试对应的 broker URL
            ::std::chrono::steady_clock::time_point lastAttemptTime {}; // 最近一次连接尝试(或断开)时刻

            explicit Impl(void) noexcept          = default;
            ~Impl(void) noexcept                  = default;
            Impl(const Impl&) noexcept            = delete;
            Impl(Impl&&) noexcept                 = delete;
            Impl& operator=(const Impl&) noexcept = delete;
            Impl& operator=(Impl&&) noexcept      = delete;
        };

    public:
        static MQTTv5Service& getInstance(void) noexcept;

        // 启动 MQTT 自治运行 (发送线程 + 自检维护线程), 这是一个幂等的操作;
        // 无可用 broker 地址时同样启动, 由维护线程周期自检, 地址就绪后自动连接
        [[nodiscard]] bool start(void) noexcept;

        // 停止 MQTT 客户端并断开连接，这是一个幂等的操作
        void stop(void) noexcept;

        // 重启 MQTT 客户端（等同于 stop() 后紧接着 start()）
        void restart(void) noexcept;

        // 设置动态 broker 地址覆盖 (SwarmCatalog 服务发现结果); 地址变化将立即唤醒自检线程重连
        void setBrokerUrlOverride(::std::string url) noexcept;

        // 检查当前是否已连接到 MQTT 服务器
        [[nodiscard]] bool isConnected(void) const noexcept;

        // 发布消息到指定的 MQTT 主题，返回是否成功入队
        [[nodiscard]] bool publish(::std::string_view topic, ::std::string_view payload) noexcept;

        // 订阅指定的 MQTT 主题
        void  subscribe(::std::string_view topic) noexcept;

        Impl& getImpl(void) noexcept
        {
            assert(impl_ != nullptr && "Impl pointer is null");
            return *impl_;
        }

        const Impl& getImplConst(void) const noexcept
        {
            assert(impl_ != nullptr && "Impl pointer is null");
            return *impl_;
        }

    private:
        explicit MQTTv5Service(void) noexcept = default;
        ~MQTTv5Service(void) noexcept;
        MQTTv5Service(const MQTTv5Service&) noexcept            = delete;
        MQTTv5Service& operator=(const MQTTv5Service&) noexcept = delete;

        friend class MqttCallback;

        // MQTT 消息发送线程主循环
        void senderLoop(void) noexcept;

        // 维护线程主循环: 周期自检 (地址等待/变化重连/断线兜底重连), 事件到达时被提前唤醒
        void maintainLoop(void) noexcept;

        // 单次自检: 解析有效地址 -> 判定连接健康度 -> 必要时重连
        void ensureBrokerConnection(void) noexcept;

        // 重建 client 并连接指定 broker 地址 (仅由维护线程调用)
        void reconnectToBroker(const ::std::string& url) noexcept;

        // 当前有效 broker 地址 (仅目录服务发现结果); 未就绪返回空串
        [[nodiscard]] ::std::string effectiveBrokerUrl(void) noexcept;

        // 兜底: 主动查询目录最近解析的 broker 地址 (广播事件为一次性, 目录可能早于本服务启动完成解析);
        // 查到非空时同步为动态覆盖并返回该地址, 否则返回空串
        [[nodiscard]] ::std::string seedBrokerUrlFromCatalog(void) noexcept;

        // 设置连接状态
        void setConnected(bool status) noexcept;

        // 将连接状态同步到域模型 (PlaneStateStore; 供本地展示与上报; url 取当前维护中的 activeUrl)
        void                                  syncConnectionStateToStore(bool connected) noexcept;

        ::std::mutex                          mutex_ {};
        ::std::string                         broker_url_override_ {}; // 动态 broker 覆盖 (Catalog 服务发现), 由 mutex_ 保护
        ::std::atomic<bool>                   running_ { false };
        ::std::atomic<bool>                   connected_ { false };
        ::std::unique_ptr<Impl>               impl_ { ::std::make_unique<Impl>() };
        constexpr static inline ::std::size_t MAX_DEQUE_SIZE { 30 };
        constexpr static inline auto          LOG_THROTTLE_INTERVAL { ::std::chrono::seconds(5) };
        constexpr static inline auto          kMaintainInterval { ::std::chrono::seconds(3) };      // 自检周期
        constexpr static inline auto          kConnectAttemptTimeout { ::std::chrono::seconds(8) }; // 单次连接尝试判定窗口
        ::std::unique_ptr<::eventpp::ScopedRemover<plane::manager::EventManager::SystemDispatcher>> system_event_remover_ {};
    };
} // namespace plane::manager

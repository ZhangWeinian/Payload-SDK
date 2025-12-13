// cy_psdk/services/MQTT/Service.h

#pragma once

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

namespace plane::services
{
	class MQTTService
	{
	protected:
		struct Impl
		{
			_STD unique_ptr<_MQTT async_client> client {};
			_STD shared_ptr<class MqttCallback> callback {};
			_STD string							serverURI {};
			_STD string							clientId {};
			int									reconnectAttempts { 0 };
			_STD_CHRONO steady_clock::time_point lastDisconnectTime {};
			_STD_CHRONO steady_clock::time_point lastConnectTime {};
			bool								 manualDisconnect { false };
			_STD deque<_STD pair<_STD string, _STD string>> messageDeque {};
			_STD mutex										dequeMutex {};
			_STD condition_variable							dequeCv {};
			_STD thread										senderThread {};
			_STD atomic<bool> runSender { false };
			bool			  isDroppingMessages { false };
			_STD_CHRONO steady_clock::time_point lastDropLogTime {};

			explicit Impl(void) noexcept		  = default;
			~Impl(void) noexcept				  = default;
			Impl(const Impl&) noexcept			  = delete;
			Impl(Impl&&) noexcept				  = delete;
			Impl& operator=(const Impl&) noexcept = delete;
			Impl& operator=(Impl&&) noexcept	  = delete;
		};

	public:
		static MQTTService& getInstance(void) noexcept;

		// 启动后端 MQTT 客户端并连接到服务器，这是一个幂等的操作
		_NODISCARD bool start(void) noexcept;

		// 停止 MQTT 客户端并断开连接，这是一个幂等的操作
		void stop(void) noexcept;

		// 重启 MQTT 客户端（等同于 stop() 后紧接着 start()）
		void restart(void) noexcept;

		// 检查当前是否已连接到 MQTT 服务器
		_NODISCARD bool isConnected(void) const noexcept;

		// 发布消息到指定的 MQTT 主题，返回是否成功入队
		_NODISCARD bool publish(_STD string_view topic, _STD string_view payload) noexcept;

		// 订阅指定的 MQTT 主题
		void  subscribe(_STD string_view topic) noexcept;

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
		explicit MQTTService(void) noexcept = default;
		~MQTTService(void) noexcept;
		MQTTService(const MQTTService&) noexcept			= delete;
		MQTTService& operator=(const MQTTService&) noexcept = delete;

		friend class MqttCallback;

		// MQTT 消息发送线程主循环
		void senderLoop(void) noexcept;

		// 设置连接状态
		void	   setConnected(bool status) noexcept;

		_STD mutex mutex_ {};
		_STD atomic<bool> running_ { false };
		_STD atomic<bool> connected_ { false };
		_STD unique_ptr<Impl>				impl_ { _STD make_unique<Impl>() };
		constexpr static inline _STD size_t MAX_DEQUE_SIZE { 30 };
		constexpr static inline auto		LOG_THROTTLE_INTERVAL { _STD_CHRONO seconds(5) };
	};
} // namespace plane::services

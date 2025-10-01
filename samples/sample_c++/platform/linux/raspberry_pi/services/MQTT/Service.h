// raspberry_pi/services/MQTT/Service.h

#pragma once

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

#include <mqtt/async_client.h>

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
			_STD atomic<bool> runSender_ { false };
			bool			  isDroppingMessages_ { false };
			_STD_CHRONO steady_clock::time_point lastDropLogTime_ {};

			explicit Impl(void) noexcept		  = default;
			~Impl(void) noexcept				  = default;
			Impl(const Impl&) noexcept			  = delete;
			Impl(Impl&&) noexcept				  = delete;
			Impl& operator=(const Impl&) noexcept = delete;
			Impl& operator=(Impl&&) noexcept	  = delete;
		};

	public:
		static MQTTService& getInstance(void) noexcept;
		_NODISCARD bool		start(_STD string_view serverURI = "", _STD string_view clientId = "") noexcept;
		void				stop(void) noexcept;
		void				restart(_STD string_view serverURI, _STD string_view clientId) noexcept;
		_NODISCARD bool		publish(_STD string_view topic, _STD string_view payload) noexcept;
		_NODISCARD bool		isConnected(void) const noexcept;
		void				subscribe(_STD string_view topic) noexcept;
		void				setConnected(bool status) noexcept;

		Impl&				getImpl(void) noexcept
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
		_STD unique_ptr<Impl> impl_ { _STD make_unique<Impl>() };
		_STD atomic<bool>					connected_ { false };
		_STD mutex							mutex_ {};
		constexpr static inline _STD size_t MAX_DEQUE_SIZE { 30 };
		constexpr static inline auto		LOG_THROTTLE_INTERVAL { _STD_CHRONO seconds(5) };

		explicit MQTTService(void) noexcept = default;
		~MQTTService(void) noexcept;
		MQTTService(const MQTTService&) noexcept			= delete;
		MQTTService& operator=(const MQTTService&) noexcept = delete;

		void		 senderLoop(void) noexcept;
	};
} // namespace plane::services

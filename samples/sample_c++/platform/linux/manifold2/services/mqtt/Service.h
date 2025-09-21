#pragma once

#include "mqtt/async_client.h"

#include <atomic>
#include <cassert>
#include <memory>
#include <mutex>
#include <string>

namespace plane::services
{
#ifndef _NODISCARD
	#define _NODISCARD [[nodiscard]]
#endif

	class MQTTService
	{
	protected:
		struct Impl
		{
			std::unique_ptr<mqtt::async_client>	  client {};
			std::shared_ptr<class MqttCallback>	  callback {};
			std::string							  serverURI {};
			std::string							  clientId {};
			int									  reconnectAttempts { 0 };
			std::chrono::steady_clock::time_point lastDisconnectTime {};
			std::chrono::steady_clock::time_point lastConnectTime {};
			bool								  manualDisconnect { false };

			explicit Impl(void) noexcept		  = default;
			~Impl(void) noexcept				  = default;
			Impl(const Impl&) noexcept			  = delete;
			Impl(Impl&&) noexcept				  = delete;
			Impl& operator=(const Impl&) noexcept = delete;
			Impl& operator=(Impl&&) noexcept	  = delete;
		};

	public:
		static MQTTService& getInstance(void) noexcept;
		_NODISCARD bool		start(const std::string& serverURI = "", const std::string& clientId = "") noexcept;
		void				stop(void) noexcept;
		void				restart(const std::string& serverURI, const std::string& clientId) noexcept;
		_NODISCARD bool		publish(const std::string& topic, const std::string& payload) noexcept;
		_NODISCARD bool		isConnected(void) const noexcept;
		void				subscribe(const std::string& topic) noexcept;
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
		std::unique_ptr<Impl> impl_ { std::make_unique<Impl>() };
		std::atomic<bool>	  connected_ { false };
		std::mutex			  mutex_ {};

		explicit MQTTService(void) noexcept = default;
		~MQTTService(void) noexcept;
		MQTTService(const MQTTService&) noexcept			= delete;
		MQTTService& operator=(const MQTTService&) noexcept = delete;
	};
} // namespace plane::services

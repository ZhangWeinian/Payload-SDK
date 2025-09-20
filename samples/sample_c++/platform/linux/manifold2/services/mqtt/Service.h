#pragma once

#include "MQTTAsync.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

void onConnectSuccess(void* context, MQTTAsync_successData* response) noexcept;
void onConnectFailure(void* context, MQTTAsync_failureData* response) noexcept;
void connectionLost(void* context, char* cause) noexcept;

namespace plane::services
{
	class MQTTService
	{
	public:
		static MQTTService& getInstance(void) noexcept;
		void				publish(const std::string& topic, const std::string& payload) noexcept;
		bool				isConnected(void) const noexcept;
		void				subscribe(const std::string& topic) const noexcept;
		void				shutdown(void) const noexcept;
		void				setConnected(bool status) noexcept;

	private:
		struct Impl;

		std::unique_ptr<Impl> impl_ {};
		std::atomic<bool>	  connected_ { false };

		friend void ::onConnectSuccess(void* context, MQTTAsync_successData* response) noexcept;
		friend void ::onConnectFailure(void* context, MQTTAsync_failureData* response) noexcept;
		friend void ::connectionLost(void* context, char* cause) noexcept;

		MQTTService(void);
		~MQTTService(void) noexcept;
		MQTTService(const MQTTService&) noexcept			= delete;
		MQTTService& operator=(const MQTTService&) noexcept = delete;
	};
} // namespace plane::services

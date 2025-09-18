#pragma once

#include "MQTTAsync.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

void onConnectSuccess(void* context, MQTTAsync_successData* response);
void onConnectFailure(void* context, MQTTAsync_failureData* response);
void connectionLost(void* context, char* cause);

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
		std::unique_ptr<Impl> impl_;
		std::atomic<bool>	  connected_ = { false };

		friend void ::onConnectSuccess(void* context, MQTTAsync_successData* response);
		friend void ::onConnectFailure(void* context, MQTTAsync_failureData* response);
		friend void ::connectionLost(void* context, char* cause);

		MQTTService(void);
		~MQTTService(void);
		MQTTService(const MQTTService&)			   = delete;
		MQTTService& operator=(const MQTTService&) = delete;
	};
} // namespace plane::services::mqtt

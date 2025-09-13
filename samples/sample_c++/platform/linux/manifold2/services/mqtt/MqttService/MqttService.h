#pragma once

#include "MQTTAsync.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

void onConnectSuccess(void* context, MQTTAsync_successData* response);
void onConnectFailure(void* context, MQTTAsync_failureData* response);
void connectionLost(void* context, char* cause);

namespace plane::services::mqtt
{
	class MQTTService
	{
	public:
		static MQTTService& getInstance();
		void				publish(const std::string& topic, const std::string& payload);
		bool				isConnected() const;
		void				subscribe(const std::string& topic);
		void				shutdown();
		void				setConnected(bool status);

	private:
		struct Impl;
		std::unique_ptr<Impl> impl_;
		std::atomic<bool>	  connected_ = { false };

		friend void ::onConnectSuccess(void* context, MQTTAsync_successData* response);
		friend void ::onConnectFailure(void* context, MQTTAsync_failureData* response);
		friend void ::connectionLost(void* context, char* cause);

		void startBackgroundThreads();
		void stopBackgroundThreads();
		void heartbeatLoop();

		MQTTService();
		~MQTTService();
		MQTTService(const MQTTService&)			   = delete;
		MQTTService& operator=(const MQTTService&) = delete;
	};
} // namespace plane::services::mqtt

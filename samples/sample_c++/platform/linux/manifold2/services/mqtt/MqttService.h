#pragma once

#include <memory>
#include <mutex>
#include <string>

namespace plane
{
	class MQTTService
	{
	public:
		// 获取单例实例的唯一方法
		static MQTTService& getInstance();

		// 公开给其他模块使用的功能接口
		void publish(const std::string& topic, const std::string& payload);
		void subscribe(const std::string& topic);

		// 查询连接状态
		bool isConnected() const;

	private:
		struct Impl;
		std::unique_ptr<Impl> impl_;

		MQTTService();
		~MQTTService();
		MQTTService(const MQTTService&)			   = delete;
		MQTTService& operator=(const MQTTService&) = delete;
	};
} // namespace plane

#include "config/ConfigManager.h"
#include "protocol/JsonProtocol.h"
#include "services/mqtt/MqttService/MqttService.h"
#include "services/mqtt/MqttTopics.h"
#include "services/telemetry/TelemetryReporter.h"
#include "utils/JsonConverter/JsonToDataClassConverter.h"
#include "utils/Logger/Logger.h"
#include "utils/NetworkUtils/NetworkUtils.h"

#include "fmt/format.h"

namespace plane::services
{
	TelemetryReporter& TelemetryReporter::getInstance(void) noexcept
	{
		static TelemetryReporter instance {};
		return instance;
	}

	TelemetryReporter::~TelemetryReporter(void) noexcept
	{
		stop();
	}

	void TelemetryReporter::start(void) noexcept
	{
		if (run_.exchange(true, std::memory_order_relaxed))
		{
			LOG_WARN("TelemetryReporter 已经启动，请勿重复调用 start()。");
			return;
		}
		LOG_INFO("正在启动遥测上报服务...");
		status_thread_ = std::thread(
			[this]()
			{
				this->statusReportLoop();
			});
		fixed_info_thread_ = std::thread(
			[this]()
			{
				this->fixedInfoReportLoop();
			});
	}

	void TelemetryReporter::stop(void) noexcept
	{
		if (!run_.exchange(false, std::memory_order_release))
		{
			return;
		}
		LOG_INFO("正在停止遥测上报服务...");
		if (status_thread_.joinable())
		{
			status_thread_.join();
		}
		if (fixed_info_thread_.joinable())
		{
			fixed_info_thread_.join();
		}
	}

	void TelemetryReporter::statusReportLoop(void) noexcept
	{
		LOG_INFO("状态上报线程已启动。");
		const auto&					   ipAddresses { plane::utils::NetworkUtils::getDeviceIpv4Address().value_or("[Not Find]") };
		plane::protocol::StatusPayload status_payload {};
		status_payload.SXZT	  = { .GPSSXSL = 15, .SFSL = 5, .SXDW = 5 };
		status_payload.DCXX	  = { .SYDL = 98, .ZDY = 22'500, .DCXXXX = { { .DCSYSDL = 98, .DY = 22'500 } } };
		status_payload.YTFY	  = -30.5;
		status_payload.YTHG	  = 0.1;
		status_payload.YTPH	  = 120.8;
		status_payload.FJFYJ  = 2.5;
		status_payload.FJHGJ  = -1.2;
		status_payload.FJPHJ  = 122.3;
		status_payload.JWD	  = 32.067228;
		status_payload.JJD	  = 118.892591;
		status_payload.JHB	  = 50.0;
		status_payload.JXDGD  = 50.0;
		status_payload.DQWD	  = 32.067228;
		status_payload.DQJD	  = 118.892591;
		status_payload.XDQFGD = 120.0;
		status_payload.JDGD	  = 170.0;
		status_payload.CZSD	  = 1.5;
		status_payload.SPSD	  = 8.7;
		status_payload.VX	  = 3.1;
		status_payload.VY	  = -2.5;
		status_payload.VZ	  = -1.5;
		status_payload.DQHD	  = 0;
		status_payload.ZHD	  = 0;
		status_payload.JGCJ	  = 0.0;
		status_payload.WZT	  = {
			   plane::protocol::VideoSource { .SPURL = fmt::format("rtsp://admin:1@{}:8554/streaming/live/1", ipAddresses),
										  .SPXY	 = "RTSP",
										  .ZBZT	 = 1 }
		};
		status_payload.CJ	= "DJI";
		status_payload.XH	= "FC30";
		status_payload.MODE = "GPS_NORMAL";
		status_payload.VSE	= 0;
		status_payload.AME	= 0;

		while (run_.load(std::memory_order_acquire))
		{
			if (!plane::services::MQTTService::getInstance().isConnected())
			{
				std::this_thread::sleep_for(std::chrono::seconds(1));
				continue;
			}

			status_payload.FJPHJ += 0.1;
			if (status_payload.FJPHJ > 180.0)
			{
				status_payload.FJPHJ -= 360.0;
			}

			std::string status_json { plane::utils::JsonConverter::buildStatusReportJson(status_payload) };
			plane::services::MQTTService::getInstance().publish(plane::services::TOPIC_DRONE_STATUS, status_json);
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
		LOG_INFO("状态上报线程已停止。");
	}

	void TelemetryReporter::fixedInfoReportLoop(void) noexcept
	{
		LOG_INFO("固定信息上报线程已启动。");
		const auto&							ipAddresses { plane::utils::NetworkUtils::getDeviceIpv4Address().value_or("[Not Find]") };
		plane::protocol::MissionInfoPayload info_payload { .FJSN   = plane::config::ConfigManager::getInstance().getPlaneCode(),
														   .YKQIP  = ipAddresses,
														   .YSRTSP = fmt::format("rtsp://admin:1@{}:8554/streaming/live/1", ipAddresses) };

		while (run_.load(std::memory_order_acquire))
		{
			if (!plane::services::MQTTService::getInstance().isConnected())
			{
				std::this_thread::sleep_for(std::chrono::seconds(1));
				continue;
			}

			std::string info_json { plane::utils::JsonConverter::buildMissionInfoJson(info_payload) };
			plane::services::MQTTService::getInstance().publish(plane::services::TOPIC_FIXED_INFO, info_json);

			for (size_t i { 0 }; i < 10; ++i)
			{
				if (!run_.load(std::memory_order_acquire))
				{
					break;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
		}
		LOG_INFO("固定信息上报线程已停止。");
	}
} // namespace plane::services

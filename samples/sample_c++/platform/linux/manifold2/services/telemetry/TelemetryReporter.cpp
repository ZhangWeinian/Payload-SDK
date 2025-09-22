#include "services/telemetry/TelemetryReporter.h"

#include "config/ConfigManager.h"
#include "protocol/DroneDataClass.h"
#include "services/mqtt/Service.h"
#include "services/mqtt/Topics.h"
#include "utils/JsonConverter/BuildAndParse.h"
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

	bool TelemetryReporter::start(void) noexcept
	{
		if (run_.exchange(true, _STD memory_order_relaxed))
		{
			LOG_DEBUG("TelemetryReporter 已经启动, 请勿重复调用 start() 。");
			return false;
		}

		LOG_DEBUG("正在启动遥测上报服务...");

		status_thread_ = _STD thread(
			[this]()
			{
				this->statusReportLoop();
				LOG_DEBUG("[start] statusReportLoop 线程启动");
			});

		fixed_info_thread_ = _STD thread(
			[this]()
			{
				this->fixedInfoReportLoop();
				LOG_DEBUG("[start] fixedInfoReportLoop 线程启动");
			});

		LOG_DEBUG("后台线程已创建, 服务启动完成");
		return true;
	}

	void TelemetryReporter::stop(void) noexcept
	{
		if (!run_.exchange(false, _STD memory_order_release))
		{
			LOG_DEBUG("已经停止, 无需重复");
			return;
		}

		if (status_thread_.joinable())
		{
			status_thread_.join();
		}

		if (fixed_info_thread_.joinable())
		{
			fixed_info_thread_.join();
		}

		LOG_DEBUG("所有线程已结束, 服务完全停止");
	}

	void TelemetryReporter::statusReportLoop(void) noexcept
	{
		_STD this_thread::sleep_for(_STD chrono::milliseconds(100));
		LOG_DEBUG("线程启动, MQTTService instance addr: {}", (void*)&plane::services::MQTTService::getInstance());

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

		while (run_.load(_STD memory_order_acquire))
		{
			if (!plane::services::MQTTService::getInstance().isConnected())
			{
				LOG_DEBUG("MQTTService 未连接, 1s 后重试");
				_STD this_thread::sleep_for(_STD chrono::seconds(1));
				continue;
			}

			status_payload.FJPHJ += 0.1;
			if (status_payload.FJPHJ > 180.0)
			{
				status_payload.FJPHJ -= 360.0;
			}

			_STD string status_json { plane::utils::JsonConverter::buildStatusReportJson(status_payload) };
			publishStatus(plane::services::TOPIC_DRONE_STATUS, status_json);

			_STD this_thread::sleep_for(_STD chrono::milliseconds(100));
		}
		LOG_DEBUG("状态上报线程已停止。");
	}

	void TelemetryReporter::fixedInfoReportLoop(void) noexcept
	{
		_STD this_thread::sleep_for(_STD chrono::milliseconds(100));
		LOG_DEBUG("固定信息上报线程已启动。");
		const auto&							ipAddresses { plane::utils::NetworkUtils::getDeviceIpv4Address().value_or("[Not Find]") };
		plane::protocol::MissionInfoPayload info_payload { .FJSN   = plane::config::ConfigManager::getInstance().getPlaneCode(),
														   .YKQIP  = ipAddresses,
														   .YSRTSP = fmt::format("rtsp://admin:1@{}:8554/streaming/live/1", ipAddresses) };

		while (run_.load(_STD memory_order_acquire))
		{
			if (!plane::services::MQTTService::getInstance().isConnected())
			{
				LOG_DEBUG("MQTTService 未连接, 1s 后重试");
				_STD this_thread::sleep_for(_STD chrono::seconds(1));
				continue;
			}

			_STD string info_json { plane::utils::JsonConverter::buildMissionInfoJson(info_payload) };
			publishStatus(plane::services::TOPIC_FIXED_INFO, info_json);

			for (size_t i { 0 }; i < 10; ++i)
			{
				if (!run_.load(_STD memory_order_acquire))
				{
					LOG_DEBUG("MQTTService 在'{}' run_ 已停止, 提前 break", plane::services::TOPIC_FIXED_INFO);
					break;
				}
				_STD this_thread::sleep_for(_STD chrono::milliseconds(100));
			}
		}
		LOG_DEBUG("固定信息上报线程已停止。");
	}

	bool TelemetryReporter::publishStatus(_STD string_view topic, _STD string_view status_json) noexcept
	{
		if (!plane::services::MQTTService::getInstance().isConnected())
		{
			LOG_DEBUG("MQTTService 未连接, 无法发布");
			return false;
		}

		try
		{
			if (!plane::services::MQTTService::getInstance().publish(topic, status_json))
			{
				LOG_DEBUG("MQTTService 在'{}' 发布失败", topic);
				return false;
			}
		}
		catch (const _STD exception& ex)
		{
			LOG_ERROR("MQTTService 在'{}' 发布时出现异常: {}", topic, ex.what());
			return false;
		}
		catch (...)
		{
			LOG_ERROR("MQTTService 在'{}' 发布时出现未知异常", topic);
			return false;
		}
		return true;
	}
} // namespace plane::services

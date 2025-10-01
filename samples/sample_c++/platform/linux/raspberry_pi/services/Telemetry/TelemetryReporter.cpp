// services/Telemetry/TelemetryReporter.cpp

#include "services/Telemetry/TelemetryReporter.h"

#include <fmt/format.h>

#include "config/ConfigManager.h"
#include "protocol/HeartbeatDataClass.h"
#include "services/DroneControl/PSDKAdapter/PSDKAdapter.h"
#include "services/MQTT/Service.h"
#include "services/MQTT/Topics.h"
#include "utils/EnvironmentCheck.h"
#include "utils/JsonConverter/BuildAndParse.h"
#include "utils/Logger.h"
#include "utils/NetworkUtils.h"

namespace plane::services
{
	TelemetryReporter& TelemetryReporter::getInstance(void) noexcept
	{
		static TelemetryReporter instance {};
		return instance;
	}

	TelemetryReporter::~TelemetryReporter(void) noexcept
	{
		this->stop();
	}

	bool TelemetryReporter::start(void) noexcept
	{
		if (this->run_.exchange(true, _STD memory_order_relaxed))
		{
			LOG_DEBUG("TelemetryReporter 已经启动, 请勿重复调用 start() 。");
			return false;
		}

		LOG_DEBUG("正在启动遥测上报服务...");

		this->status_thread_ = _STD thread(
			[this]()
			{
				this->statusReportLoop();
				LOG_DEBUG("statusReportLoop 线程启动");
			});

		this->fixed_info_thread_ = _STD thread(
			[this]()
			{
				this->fixedInfoReportLoop();
				LOG_DEBUG("fixedInfoReportLoop 线程启动");
			});

		LOG_DEBUG("后台线程已创建, 服务启动完成");
		return true;
	}

	void TelemetryReporter::stop(void) noexcept
	{
		if (!this->run_.exchange(false, _STD memory_order_release))
		{
			LOG_DEBUG("已经停止, 无需重复");
			return;
		}

		if (this->status_thread_.joinable())
		{
			this->status_thread_.join();
		}

		if (this->fixed_info_thread_.joinable())
		{
			this->fixed_info_thread_.join();
		}

		LOG_DEBUG("所有线程已结束, 服务完全停止");
	}

	void TelemetryReporter::statusReportLoop(void) noexcept
	{
		_STD this_thread::sleep_for(_STD_CHRONO milliseconds(100));
		LOG_DEBUG("线程启动, MQTTService instance addr: {}", (void*)&plane::services::MQTTService::getInstance());

		const auto& ipAddresses { plane::utils::NetworkUtils::getInstance().getDeviceIpv4Address().value_or("[Not Find]") };
		const plane::protocol::VideoSource defaultVideoSource { .SPURL = _FMT format("rtsp://admin:1@{}:8554/streaming/live/1", ipAddresses),
																.SPXY  = "RTSP",
																.ZBZT  = 1 };

		const bool						   psdk_enabled { plane::utils::isStandardProceduresEnabled() };
		if (!psdk_enabled)
		{
			LOG_WARN("PSDK 未启用 (FULL_PSDK!=1)，遥测上报将使用模拟数据。");
		}

		while (this->run_.load(_STD memory_order_acquire))
		{
			if (!plane::services::MQTTService::getInstance().isConnected())
			{
				LOG_DEBUG("MQTTService 未连接, 1s 后重试");
				_STD this_thread::sleep_for(_STD_CHRONO seconds(1));
				continue;
			}

			const auto					   now { _STD_CHRONO steady_clock::now() };
			const auto					   lastUpdate { plane::services::PSDKAdapter::getInstance().getLastUpdateTime() };
			const bool					   isDataStale { now - lastUpdate > this->PSDK_WATCHDOG_TIMEOUT };

			plane::protocol::StatusPayload status_payload {};
			if (isDataStale)
			{
				LOG_ERROR("PSDK 数据采集线程看门狗超时！数据已超过 {} 秒未更新！", this->PSDK_WATCHDOG_TIMEOUT.count());

				status_payload				= plane::services::PSDKAdapter::getInstance().getLatestStatusPayload();
				status_payload.SXZT.GPSSXSL = -99;
			}
			else
			{
				if (psdk_enabled)
				{
					status_payload = plane::services::PSDKAdapter::getInstance().getLatestStatusPayload();
				}
			}

			status_payload.WZT = { defaultVideoSource };
			_STD string status_json { plane::utils::JsonConverter::buildStatusReportJson(status_payload) };
			publishStatus(plane::services::TOPIC_DRONE_STATUS, status_json);

			_STD this_thread::sleep_for(_STD_CHRONO milliseconds(100));
		}
		LOG_DEBUG("状态上报线程已停止。");
	}

	void TelemetryReporter::fixedInfoReportLoop(void) noexcept
	{
		_STD this_thread::sleep_for(_STD_CHRONO milliseconds(100));
		LOG_DEBUG("固定信息上报线程已启动。");
		const auto& ipAddresses { plane::utils::NetworkUtils::getInstance().getDeviceIpv4Address().value_or("[Not Find]") };
		plane::protocol::MissionInfoPayload info_payload { .FJSN   = plane::config::ConfigManager::getInstance().getPlaneCode(),
														   .YKQIP  = ipAddresses,
														   .YSRTSP = _FMT format("rtsp://admin:1@{}:8554/streaming/live/1", ipAddresses) };

		while (this->run_.load(_STD memory_order_acquire))
		{
			if (!plane::services::MQTTService::getInstance().isConnected())
			{
				LOG_DEBUG("MQTTService 未连接, 1s 后重试");
				_STD this_thread::sleep_for(_STD_CHRONO seconds(1));
				continue;
			}

			_STD string info_json { plane::utils::JsonConverter::buildMissionInfoJson(info_payload) };
			publishStatus(plane::services::TOPIC_FIXED_INFO, info_json);

			for (_STD size_t i { 0 }; i < 10; ++i)
			{
				if (!this->run_.load(_STD memory_order_acquire))
				{
					LOG_DEBUG("MQTTService 在'{}' run_ 已停止, 提前 break", plane::services::TOPIC_FIXED_INFO);
					break;
				}
				_STD this_thread::sleep_for(_STD_CHRONO milliseconds(100));
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

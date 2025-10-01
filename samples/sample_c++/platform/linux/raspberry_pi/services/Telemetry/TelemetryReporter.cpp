// raspberry_pi/services/Telemetry/TelemetryReporter.cpp

#include "TelemetryReporter.h"

#include "config/ConfigManager.h"
#include "services/MQTT/Service.h"
#include "services/MQTT/Topics.h"
#include "utils/EnvironmentCheck.h"
#include "utils/JsonConverter/BuildAndParse.h"
#include "utils/Logger.h"
#include "utils/NetworkUtils.h"

#include <fmt/format.h>

#include <variant>

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

	bool TelemetryReporter::start(void)
	{
		if (!this->removers_)
		{
			LOG_DEBUG("TelemetryReporter 已经启动，请勿重复调用 start()。");
			return false;
		}

		try
		{
			auto&				   dispatcher { plane::services::PSDKAdapter::getInstance().getEventDispatcher() };
			this->removers_ = _STD make_unique<_EVENTPP ScopedRemover<plane::services::PSDKAdapter::EventDispatcher>>(dispatcher);

			this->removers_->appendListener(plane::services::PSDKAdapter::PsdkEvent::TelemetryUpdated,
											[this](const plane::services::PSDKAdapter::EventData& data)
											{
												this->onPsdkEvent(data);
											});

			this->removers_->appendListener(plane::services::PSDKAdapter::PsdkEvent::MissionStateChanged,
											[this](const plane::services::PSDKAdapter::EventData& data)
											{
												this->onPsdkEvent(data);
											});

			this->removers_->appendListener(plane::services::PSDKAdapter::PsdkEvent::ActionStateChanged,
											[this](const plane::services::PSDKAdapter::EventData& data)
											{
												this->onPsdkEvent(data);
											});

			LOG_INFO("遥测上报服务已启动。");
			return true;
		}
		catch (const _STD exception& ex)
		{
			LOG_ERROR("遥测上报服务启动失败，出现异常: {}", ex.what());
			this->stop();
			return false;
		}
		catch (...)
		{
			LOG_ERROR("遥测上报服务启动失败，出现未知异常");
			this->stop();
			return false;
		}
	}

	void TelemetryReporter::stop(void)
	{
		if (!this->removers_)
		{
			this->removers_.reset();
			LOG_INFO("遥测上报服务已停止 (注销了所有事件监听器)。");
		}
	}

	bool TelemetryReporter::publishJson(_STD string_view topic, _STD string_view status_json) noexcept
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

	void TelemetryReporter::onPsdkEvent(const plane::services::PSDKAdapter::EventData& eventData)
	{
		_STD visit(
			[this](const auto& data)
			{
				using T = _STD decay_t<decltype(data)>;

				if constexpr (_STD is_same_v<T, plane::protocol::StatusPayload>)
				{
					if (!plane::services::MQTTService::getInstance().isConnected())
					{
						return;
					}

					auto	   payload { data };
					const auto now { _STD_CHRONO steady_clock::now() };
					const auto lastUpdate { plane::services::PSDKAdapter::getInstance().getLastUpdateTime() };
					if (now - lastUpdate > this->PSDK_WATCHDOG_TIMEOUT)
					{
						LOG_ERROR("PSDK 数据采集线程看门狗超时！数据已超过 {} 秒未更新！", this->PSDK_WATCHDOG_TIMEOUT.count());
						payload.SXZT.GPSSXSL = -99;
					}

					static const auto ip { plane::utils::NetworkUtils::getInstance().getDeviceIpv4Address().value_or("[Not Find]") };
					payload.WZT = {
						plane::protocol::VideoSource { .SPURL = _FMT format("rtsp://admin:1@{}:8554/streaming/live/1", ip),
													   .SPXY  = "RTSP",
													   .ZBZT  = 1 }
					};
					this->publishJson(plane::services::TOPIC_DRONE_STATUS, plane::utils::JsonConverter::buildStatusReportJson(payload));

					if (static int telemetry_counter { 0 }; ++telemetry_counter >= 50)
					{
						telemetry_counter = 0;
						plane::protocol::MissionInfoPayload info_payload { .FJSN  = plane::config::ConfigManager::getInstance().getPlaneCode(),
																		   .YKQIP = ip,
																		   .YSRTSP =
																			   _FMT format("rtsp://admin:1@{}:8554/streaming/live/1", ip) };
						this->publishJson(plane::services::TOPIC_FIXED_INFO, plane::utils::JsonConverter::buildMissionInfoJson(info_payload));
					}
				}
				else if constexpr (_STD is_same_v<T, T_DjiWaypointV3MissionState>)
				{
					// 在这里，我们可以将航线状态上报给地面站
					// 例如，构建一个新的 MQTT 消息
					LOG_DEBUG("接收到航线状态更新，准备上报...");
					// TODO: 实现 MissionProgressPayload 的构建和上报
				}
				else if constexpr (_STD is_same_v<T, T_DjiWaypointV3ActionState>)
				{
					LOG_DEBUG("接收到航线动作更新...");
					// TODO: 根据需要处理或上报动作状态
				}
			},
			eventData);
	}
} // namespace plane::services

// raspberry_pi/services/Telemetry/TelemetryReporter.cpp

#include "TelemetryReporter.h"

#include "config/ConfigManager.h"
#include "services/MQTT/Service.h"
#include "services/MQTT/Topics.h"
#include "utils/JsonConverter/BuildAndParse.h"
#include "utils/Logger.h"
#include "utils/NetworkUtils.h"

#include <fmt/format.h>
#include <gsl/gsl>

#include <variant>

namespace plane::services
{
	TelemetryReporter& TelemetryReporter::getInstance(void) noexcept
	{
		static TelemetryReporter instance {};
		return instance;
	}

	TelemetryReporter::TelemetryReporter(void) noexcept:
		event_processing_pool_(_STD make_unique<_THREADPOOL ThreadPool>(2)),
		last_health_ping_time_(_STD chrono::steady_clock::now())
	{}

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
			auto&				   dispatcher { plane::services::EventManager::getInstance().getStatusDispatcher() };
			this->removers_ = _STD make_unique<_EVENTPP ScopedRemover<plane::services::EventManager::StatusDispatcher>>(dispatcher);

			this->removers_->appendListener(plane::services::EventManager::PSDKEvent::TelemetryUpdated,
											[this](const plane::services::EventManager::PSDKEventData& data)
											{
												this->onPsdkEvent(data);
											});

			this->removers_->appendListener(plane::services::EventManager::PSDKEvent::MissionStateChanged,
											[this](const plane::services::EventManager::PSDKEventData& data)
											{
												this->onPsdkEvent(data);
											});

			this->removers_->appendListener(plane::services::EventManager::PSDKEvent::ActionStateChanged,
											[this](const plane::services::EventManager::PSDKEventData& data)
											{
												this->onPsdkEvent(data);
											});

			this->removers_->appendListener(plane::services::EventManager::PSDKEvent::HealthPing,
											[this](const plane::services::EventManager::PSDKEventData& data)
											{
												if (auto* p_time { _STD get_if<_STD_CHRONO steady_clock::time_point>(&data) })
												{
													this->last_health_ping_time_ = *p_time;
												}
											});

			this->removers_->appendListener(plane::services::EventManager::PSDKEvent::HealthStatusUpdated,
											[this](const plane::services::EventManager::PSDKEventData& data)
											{
												this->onPsdkEvent(data);
											});

			this->run_watchdog_ = true;
			this->event_processing_pool_->enqueue(
				[this]
				{
					this->runWatchdogCheck();
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
		this->run_watchdog_ = false;

		if (!this->removers_)
		{
			this->removers_.reset();
			LOG_DEBUG("遥测上报服务已停止 (注销了所有事件监听器)。");
		}

		if (this->event_processing_pool_)
		{
			this->event_processing_pool_.reset();
			LOG_DEBUG("遥测上报服务已停止 (关闭事件处理线程池)。");
		}

		LOG_INFO("遥测上报服务已停止。");
	}

	bool TelemetryReporter::publishJson(_STD string_view topic, _STD string_view statusJson) noexcept
	{
		if (!plane::services::MQTTService::getInstance().isConnected())
		{
			LOG_DEBUG("MQTTService 未连接, 无法发布");
			return false;
		}

		try
		{
			if (!plane::services::MQTTService::getInstance().publish(topic, statusJson))
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

	void TelemetryReporter::onPsdkEvent(const plane::services::EventManager::PSDKEventData& eventData)
	{
		if (!this->event_processing_pool_)
		{
			LOG_WARN("事件处理线程池未初始化，无法处理 PSDK 事件");
			return;
		}

		if (this->queued_task_count_ >= this->MAX_EVENT_QUEUE_SIZE)
		{
			static _STD_CHRONO steady_clock::time_point last_log_time {};
			auto										now { _STD_CHRONO steady_clock::now() };
			if (now - last_log_time > _STD_CHRONO seconds(5))
			{
				LOG_WARN("TelemetryReporter 事件处理队列已满 (超过 {} 个任务)，正在丢弃新事件。", MAX_EVENT_QUEUE_SIZE);
				last_log_time = now;
			}
			return;
		}

		++(this->queued_task_count_);

		this->event_processing_pool_->enqueue(
			[this, eventData]
			{
				auto counter_guard = _GSL finally(
					[this]
					{
						--(this->queued_task_count_);
					});

				_STD visit(
					[this](const auto& event)
					{
						using T = _STD decay_t<decltype(event)>;

						if constexpr (_STD is_same_v<T, plane::protocol::StatusPayload>)
						{
							if (!plane::services::MQTTService::getInstance().isConnected())
							{
								return;
							}

							auto			  payload { event };
							static const auto ip { plane::utils::NetworkUtils::getInstance().getDeviceIpv4Address().value_or("[Not Find]") };

							static int		  status_counter { 0 };
							if (++status_counter >= 5)
							{
								status_counter = 0;
								payload.WZT	   = {
									   plane::protocol::VideoSource { .SPURL = _FMT format("rtsp://admin:1@{}:8554/streaming/live/1", ip),
																  .SPXY	 = "RTSP",
																  .ZBZT	 = 1 }
								};
								this->publishJson(plane::services::TOPIC_DRONE_STATUS,
												  plane::utils::JsonConverter::buildStatusReportJson(payload));
							}

							static int fixed_info_counter { 0 };
							if (++fixed_info_counter >= 50)
							{
								fixed_info_counter = 0;
								plane::protocol::MissionInfoPayload info_payload {
									.FJSN	= plane::config::ConfigManager::getInstance().getPlaneCode(),
									.YKQIP	= ip,
									.YSRTSP = _FMT format("rtsp://admin:1@{}:8554/streaming/live/1", ip)
								};
								this->publishJson(plane::services::TOPIC_FIXED_INFO,
												  plane::utils::JsonConverter::buildMissionInfoJson(info_payload));
							}
						}
						else if constexpr (_STD is_same_v<T, plane::protocol::HealthStatusPayload>)
						{
							LOG_INFO("准备上报健康状态...");
							this->publishJson(plane::services::TOPIC_HEALTH_MANAGE, plane::utils::JsonConverter::buildHealthStatusJson(event));
						}
						else if constexpr (_STD is_same_v<T, _DJI T_DjiWaypointV3MissionState>)
						{
							plane::protocol::MissionProgressPayload progress {};
							progress.ZT	  = static_cast<int>(event.state);
							progress.DQHD = event.currentWaypointIndex;
							progress.RWID = _STD to_string(event.wayLineId);
							// this->publishJson(plane::services::TOPIC_MISSION_PROGRESS,
							// 				  plane::utils::JsonConverter::buildMissionProgressJson(progress));
						}
						else if constexpr (_STD is_same_v<T, _DJI T_DjiWaypointV3ActionState>)
						{
							LOG_DEBUG("接收到航线动作更新...");
							// TODO: 根据需要处理或上报动作状态
						}
						else
						{
							LOG_WARN("收到未知类型的 PSDK 事件数据");
						}
					},
					eventData);
			});
	}

	void TelemetryReporter::runWatchdogCheck(void) noexcept
	{
		if (!this->run_watchdog_)
		{
			LOG_INFO("看门狗任务收到停止信号，不再调度下一次检查。");
			return;
		}

		const auto now { _STD_CHRONO steady_clock::now() };
		const auto lastUpdate { this->last_health_ping_time_.load() };
		if (now - lastUpdate > this->PSDK_WATCHDOG_TIMEOUT)
		{
			LOG_ERROR("看门狗超时！PSDK 数据源已超过 {} 秒没有更新！", this->PSDK_WATCHDOG_TIMEOUT.count());
		}
		else
		{
			LOG_TRACE("看门狗检查通过，PSDK 数据源正常。");
		}

		this->event_processing_pool_->enqueue(
			[this]
			{
				_STD this_thread::sleep_for(this->PSDK_WATCHDOG_CHECK_INTERVAL);
				this->runWatchdogCheck();
			});
	}
} // namespace plane::services

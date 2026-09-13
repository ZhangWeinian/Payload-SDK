// cy_psdk/manager/telemetry/TelemetryReporter.cpp

#include "manager/telemetry/TelemetryReporter.h"

#include "config/ConfigManager.h"
#include "manager/mqtt/MQTTTopics.h"
#include "manager/mqtt/service/MQTTv5Service.h"
#include "manager/plane_state/PlaneStateStore.h"
#include "manager/psdk/PSDKAdapter.h"
#include "utils/json_converter/BuildAndParse.h"
#include "utils/log_util/Logger.h"
#include "utils/network_util/GetLocalIPV4.h"
#include "utils/rtsp_util/RtspUrl.h"

#include <fmt/format.h>
#include <gsl/gsl>

#include <variant>

namespace plane::manager
{
    TelemetryReporter& TelemetryReporter::getInstance(void) noexcept
    {
        static TelemetryReporter instance {};
        return instance;
    }

    TelemetryReporter::TelemetryReporter(void) noexcept: event_processing_pool_(::std::make_unique<::BS::thread_pool<>>(6)),
                                                         last_health_ping_time_(::std::chrono::steady_clock::now())
    {}

    TelemetryReporter::~TelemetryReporter(void) noexcept
    {
        try
        {
            this->stop();
        }
        catch (const ::std::exception& e)
        {
            LOG_ERROR("遥测上报服务析构异常: {}", e.what());
        }
        catch (...)
        {
            LOG_ERROR("遥测上报服务析构发生未知异常: <non-std exception>");
        }
    }

    bool TelemetryReporter::start(void)
    {
        if (bool expected { false }; !this->running_.compare_exchange_strong(expected, true))
        {
            LOG_WARN("TelemetryReporter::start() 被重复调用，已忽略");
            return true;
        }
        else
        {
            LOG_DEBUG("TelemetryReporter::start() 正在执行");
        }

        try
        {
            auto& dispatcher { plane::manager::EventManager::getInstance().getStatusDispatcher() };
            this->psdk_event_remover_ = ::std::make_unique<::eventpp::ScopedRemover<plane::manager::EventManager::StatusDispatcher>>(dispatcher);

            this->psdk_event_remover_->appendListener(
                plane::manager::EventManager::PSDKEvent::MissionStateChanged,
                [this](const plane::manager::EventManager::PSDKEventData& data)
                {
                    this->onPSDKEvent(data);
                }
            );

            this->psdk_event_remover_->appendListener(
                plane::manager::EventManager::PSDKEvent::ActionStateChanged,
                [this](const plane::manager::EventManager::PSDKEventData& data)
                {
                    this->onPSDKEvent(data);
                }
            );

            this->psdk_event_remover_->appendListener(
                plane::manager::EventManager::PSDKEvent::HealthPing,
                [this](const plane::manager::EventManager::PSDKEventData& data)
                {
                    if (auto* p_time { ::std::get_if<::std::chrono::steady_clock::time_point>(&data) })
                    {
                        this->last_health_ping_time_ = *p_time;
                    }
                }
            );

            this->psdk_event_remover_->appendListener(
                plane::manager::EventManager::PSDKEvent::HealthStatusUpdated,
                [this](const plane::manager::EventManager::PSDKEventData& data)
                {
                    this->onPSDKEvent(data);
                }
            );

            // 上报节拍自治: 本组件自持定时线程 (STATUS 10Hz / FIXED_INFO 1Hz),
            // 不从 PSDK 采集事件或心跳 tick 借频率
            this->report_thread_ = ::std::thread(&TelemetryReporter::runReportLoop, this);
            LOG_INFO("上报节拍线程已启动 (STATUS 10Hz / FIXED_INFO 1Hz)");

            if (plane::config::ConfigManager::getInstance().isStandardProceduresEnabled())
            {
                this->run_watchdog_ = true;
                this->event_processing_pool_->detach_task(
                    [this]
                    {
                        this->runWatchdogCheck();
                    }
                );
                LOG_INFO("PSDK 看门狗已启动");
            }
            else
            {
                LOG_INFO("PSDK 未启用，看门狗将不会启动");
            }

            LOG_INFO("遥测上报服务已启动");

            return true;
        }
        catch (const ::std::exception& ex)
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
        if (bool expected { true }; !this->running_.compare_exchange_strong(expected, false))
        {
            return;
        }

        this->run_watchdog_ = false;

        if (this->report_thread_.joinable())
        {
            this->report_thread_.join();
            LOG_DEBUG("遥测上报服务已停止 (上报节拍线程已退出)");
        }

        if (this->psdk_event_remover_)
        {
            this->psdk_event_remover_.reset();
            LOG_DEBUG("遥测上报服务已停止 (注销了所有 PSDK 事件监听器)");
        }

        if (this->event_processing_pool_)
        {
            this->event_processing_pool_.reset();
            LOG_DEBUG("遥测上报服务已停止 (关闭事件处理线程池)");
        }

        LOG_INFO("遥测上报服务已停止");
    }

    bool TelemetryReporter::publishJson(::std::string_view topic, ::std::string_view statusJson) noexcept
    {
        if (!plane::manager::MQTTv5Service::getInstance().isConnected())
        {
            LOG_DEBUG("MQTTv5Service 未连接, 无法发布");
            return false;
        }

        try
        {
            if (!plane::manager::MQTTv5Service::getInstance().publish(topic, statusJson))
            {
                LOG_DEBUG("MQTTv5Service 在'{}' 发布失败", topic);
                return false;
            }
        }
        catch (const ::std::exception& ex)
        {
            LOG_ERROR("MQTTv5Service 在'{}' 发布时出现异常: {}", topic, ex.what());
            return false;
        }
        catch (...)
        {
            LOG_ERROR("MQTTv5Service 在'{}' 发布时出现未知异常", topic);
            return false;
        }
        return true;
    }

    void TelemetryReporter::onPSDKEvent(const plane::manager::EventManager::PSDKEventData& eventData)
    {
        if (!this->event_processing_pool_)
        {
            LOG_WARN("事件处理线程池未初始化，无法处理 PSDK 事件");
            return;
        }

        if (this->queued_task_count_ >= this->MAX_EVENT_QUEUE_SIZE)
        {
            // 日志节流: 仅日志使用, 多线程下以原子毫秒计数避免数据竞争
            static ::std::atomic<int64_t> last_log_ms { 0 };
            const int64_t                 now_ms {
                ::std::chrono::duration_cast<::std::chrono::milliseconds>(::std::chrono::steady_clock::now().time_since_epoch()).count()
            };
            int64_t prev_ms { last_log_ms.load(::std::memory_order_relaxed) };
            if (now_ms - prev_ms > 5000 && last_log_ms.compare_exchange_strong(prev_ms, now_ms, ::std::memory_order_relaxed))
            {
                LOG_WARN("TelemetryReporter 事件处理队列已满 (超过 {} 个任务)，正在丢弃新事件", MAX_EVENT_QUEUE_SIZE);
            }
            return;
        }

        ++(this->queued_task_count_);

        this->event_processing_pool_->detach_task(
            [this, eventData]
            {
                auto counter_guard = ::gsl::finally(
                    [this]
                    {
                        --(this->queued_task_count_);
                    }
                );

                ::std::visit(
                    [this](const auto& event)
                    {
                        using T = ::std::decay_t<decltype(event)>;

                        if constexpr (::std::is_same_v<T, ::std::chrono::steady_clock::time_point>)
                        {
                            this->last_health_ping_time_ = event;
                            return;
                        }
                        else if constexpr (::std::is_same_v<T, plane::protocol::HealthStatusPayload>)
                        {
                            LOG_DEBUG("准备上报健康状态");

                            (void)this
                                ->publishJson(plane::manager::TOPIC_HEALTH_MANAGE, plane::utils::JsonConverter::buildHealthStatusJson(event));
                        }
                        else if constexpr (::std::is_same_v<T, ::T_DjiWaypointV3MissionState>)
                        {
                            plane::protocol::MissionProgressPayload progress {};
                            progress.ZT   = static_cast<int>(event.state);
                            progress.DQHD = event.currentWaypointIndex;
                            progress.RWID = ::std::to_string(event.wayLineId);
                            // (void)this->publishJson(plane::manager::TOPIC_MISSION_PROGRESS,
                            // 				  plane::utils::JsonConverter::buildMissionProgressJson(progress));
                        }
                        else if constexpr (::std::is_same_v<T, ::T_DjiWaypointV3ActionState>)
                        {
                            LOG_DEBUG("接收到航线动作更新");
                            // TODO: 根据需要处理或上报动作状态
                        }
                        else
                        {
                            LOG_WARN("收到未知类型的 PSDK 事件数据");
                        }
                    },
                    eventData
                );
            }
        );
    }

    void TelemetryReporter::runReportLoop(void) noexcept
    {
        // 节拍自治: STATUS 固定 10Hz; 每第 FIXED_INFO_EVERY_N_TICKS 拍附发一次 FIXED_INFO (1Hz)。
        // 数据的新旧/真假不由发送者评判: PSDK 未连/序列号未就绪时也照常按节拍发出。
        int  tick { 0 };
        auto next_wakeup { ::std::chrono::steady_clock::now() };
        while (this->running_)
        {
            next_wakeup += this->STATUS_REPORT_INTERVAL;
            this->publishStatusReport();
            if (++tick >= this->FIXED_INFO_EVERY_N_TICKS)
            {
                tick = 0;
                this->publishFixedInfo();
            }
            ::std::this_thread::sleep_until(next_wakeup);
        }
    }

    void TelemetryReporter::publishStatusReport(void) noexcept
    {
        if (!plane::manager::MQTTv5Service::getInstance().isConnected())
        {
            return; // 未连接: 静默跳过本拍 (节拍相位不受影响, 连接恢复后自动接续)
        }

        // 数据源: PSDK 适配器维护的最新状态负载 (拉取式读取, 与采集频率无关)
        plane::protocol::StatusPayload payload { plane::manager::PSDKAdapter::getInstance().getLatestStatusPayload() };

        // 视频源: 本机 RTSP 推流地址 (域模型字段级读取, 不整份快照); IP 未就绪/配置不完整时不含视频源
        const auto [rtsp_user, rtsp_password, rtsp_base_url, rtsp_port] { plane::domain::PlaneStateStore::getInstance().read(
            &plane::domain::PlaneStateDataClass::rtsp_push_video_user_name,
            &plane::domain::PlaneStateDataClass::rtsp_push_video_password,
            &plane::domain::PlaneStateDataClass::rtsp_push_video_base_url,
            &plane::domain::PlaneStateDataClass::rtsp_push_video_server_port
        ) };
        if (const ::std::string rtsp_url { plane::utils::buildLocalRtspUrl(rtsp_user, rtsp_password, rtsp_base_url, rtsp_port) };
            !rtsp_url.empty())
        {
            payload.WZT = {
                plane::protocol::VideoSource { .SPURL = rtsp_url, .SPXY = "RTSP", .ZBZT = 1 }
            };
        }

        (void)this->publishJson(plane::manager::TOPIC_STATUS, plane::utils::JsonConverter::buildStatusReportJson(payload));
    }

    void TelemetryReporter::publishFixedInfo(void) noexcept
    {
        if (!plane::manager::MQTTv5Service::getInstance().isConnected())
        {
            return;
        }

        // 字段级读取 (不整份快照): 序列号 + RTSP 配置
        const auto [serial_number, rtsp_user, rtsp_password, rtsp_base_url, rtsp_port] { plane::domain::PlaneStateStore::getInstance().read(
            &plane::domain::PlaneStateDataClass::serial_number,
            &plane::domain::PlaneStateDataClass::rtsp_push_video_user_name,
            &plane::domain::PlaneStateDataClass::rtsp_push_video_password,
            &plane::domain::PlaneStateDataClass::rtsp_push_video_base_url,
            &plane::domain::PlaneStateDataClass::rtsp_push_video_server_port
        ) };

        static const auto                         ip_address { plane::utils::getLocalIPV4().value_or("N/A") };

        const plane::protocol::MissionInfoPayload info_payload {
            .FJSN   = serial_number,
            .YKQIP  = ip_address,
            .YSRTSP = plane::utils::buildLocalRtspUrl(rtsp_user, rtsp_password, rtsp_base_url, rtsp_port)
        };

        (void)this->publishJson(plane::manager::TOPIC_FIXED_INFO, plane::utils::JsonConverter::buildMissionInfoJson(info_payload));
    }

    void TelemetryReporter::runWatchdogCheck(void) noexcept
    {
        if (!this->run_watchdog_)
        {
            LOG_INFO("看门狗任务收到停止信号，不再调度下一次检查");
            return;
        }

        const auto now { ::std::chrono::steady_clock::now() };
        const auto last_update { this->last_health_ping_time_.load() };
        if (now - last_update > this->PSDK_WATCHDOG_TIMEOUT)
        {
            LOG_ERROR("看门狗超时！PSDK 数据源已超过 {} 秒没有更新！", this->PSDK_WATCHDOG_TIMEOUT.count());
        }
        else
        {
            LOG_TRACE("看门狗检查通过，PSDK 数据源正常");
        }

        this->event_processing_pool_->detach_task(
            [this]
            {
                ::std::this_thread::sleep_for(this->PSDK_WATCHDOG_CHECK_INTERVAL);
                this->runWatchdogCheck();
            }
        );
    }
} // namespace plane::manager

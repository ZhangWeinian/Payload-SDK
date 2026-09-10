// cy_psdk/manager/psdk/PSDKAdapter.cpp

#include "manager/psdk/PSDKAdapter.h"

#include <dji_aircraft_info.h>
#include <dji_camera_manager.h>
#include <dji_error.h>
#include <dji_flight_controller.h>
#include <dji_gimbal.h>
#include <dji_hms_info_table.h>
#include <dji_logger.h>
#include <dji_waypoint_v3.h>

#include "config/ConfigManager.h"
#include "manager/plane_state/PlaneStateStore.h"
#include "utils/DjiErrorUtils.h"
#include "utils/log_util/Logger.h"

#include <fmt/format.h>
#include <gsl/gsl>

#include <string_view>
#include <cmath>
#include <filesystem>

namespace plane::manager
{
	using namespace _STD literals;

	namespace
	{
		// STATUS_DISPLAYMODE 原始码 -> 域模型 FlightMode (对齐 msdk 语义; 未映射 -> UNKNOWN)
		inline plane::domain::FlightMode displayModeToFlightMode(int display_mode_code) noexcept
		{
			switch (display_mode_code)
			{
				case 0:
					return plane::domain::FlightMode::MANUAL;
				case 1:
					return plane::domain::FlightMode::ATTI;
				case 6:
					return plane::domain::FlightMode::GPS_NORMAL;
				case 10:
				case 11:
					return plane::domain::FlightMode::AUTO_TAKE_OFF;
				case 12:
					return plane::domain::FlightMode::AUTO_LANDING;
				case 15:
					return plane::domain::FlightMode::GO_HOME;
				case 33:
					return plane::domain::FlightMode::FORCE_LANDING;
				default:
					return plane::domain::FlightMode::UNKNOWN;
			}
		}

		// 将飞机型号枚举转换为字符串
		inline _STD string_view aircraftTypeToString(_DJI E_DjiAircraftType type)
		{
			switch (type)
			{
				// M200 V2 Series
				case _DJI DJI_AIRCRAFT_TYPE_M200_V2:
					return "Matrice 200 V2"sv;
				case _DJI DJI_AIRCRAFT_TYPE_M210_V2:
					return "Matrice 210 V2"sv;
				case _DJI DJI_AIRCRAFT_TYPE_M210RTK_V2:
					return "Matrice 210 RTK V2"sv;

				// M300 / M350 Series
				case _DJI DJI_AIRCRAFT_TYPE_M300_RTK:
					return "Matrice 300 RTK"sv;
				case _DJI DJI_AIRCRAFT_TYPE_M350_RTK:
					return "Matrice 350 RTK"sv;

				// M30 Series
				case _DJI DJI_AIRCRAFT_TYPE_M30:
					return "Matrice 30"sv;
				case _DJI DJI_AIRCRAFT_TYPE_M30T:
					return "Matrice 30T"sv;

				// Mavic 3 Enterprise Series
				case _DJI DJI_AIRCRAFT_TYPE_M3E:
					return "Mavic 3 Enterprise"sv;
				case _DJI DJI_AIRCRAFT_TYPE_M3T:
					return "Mavic 3 Thermal"sv;
				case _DJI DJI_AIRCRAFT_TYPE_M3TA:
					return "Mavic 3 TA"sv;

				// Matrice 3D / 3TD Series
				case _DJI DJI_AIRCRAFT_TYPE_M3D:
					return "Matrice 3D"sv;
				case _DJI DJI_AIRCRAFT_TYPE_M3TD:
					return "Matrice 3TD"sv;

				// Matrice 4 Series
				case _DJI DJI_AIRCRAFT_TYPE_M4T:
					return "Matrice 4T"sv;
				case _DJI DJI_AIRCRAFT_TYPE_M4E:
					return "Matrice 4E"sv;
				case _DJI DJI_AIRCRAFT_TYPE_M4TD:
					return "Matrice 4TD"sv;
				case _DJI DJI_AIRCRAFT_TYPE_M4D:
					return "Matrice 4D"sv;

				// Matrice 400 Series
				case _DJI DJI_AIRCRAFT_TYPE_M400:
					return "Matrice 400"sv;

				// Other
				case _DJI DJI_AIRCRAFT_TYPE_FC30:
					return "FlyCart 30"sv;
				case _DJI DJI_AIRCRAFT_TYPE_UNKNOWN:
				default:
					return "Unknown Aircraft"sv;
			}
		}

		// 将 PSDK 航线任务状态枚举转换为字符串
		inline _STD string_view djiMissionStateToString(_DJI E_DjiWaypointV3MissionState state)
		{
			switch (state)
			{
				case _DJI DJI_WAYPOINT_V3_MISSION_STATE_IDLE:
					return "空闲"sv;
				case _DJI DJI_WAYPOINT_V3_MISSION_STATE_PREPARE:
					return "准备中"sv;
				case _DJI DJI_WAYPOINT_V3_MISSION_STATE_TRANS_MISSION:
					return "传输中"sv;
				case _DJI DJI_WAYPOINT_V3_MISSION_STATE_MISSION:
					return "任务执行中"sv;
				case _DJI DJI_WAYPOINT_V3_MISSION_STATE_BREAK:
					return "任务中断"sv;
				case _DJI DJI_WAYPOINT_V3_MISSION_STATE_RESUME:
					return "任务恢复中"sv;
				case _DJI DJI_WAYPOINT_V3_MISSION_STATE_RETURN_FIRSTPOINT:
					return "返回航线起点"sv;
				default:
					return "未知状态"sv;
			}
		}

		// 将 PSDK 航线任务动作状态枚举转换为字符串
		inline _STD string_view djiActionStateToString(_DJI E_DjiWaypointV3ActionState state)
		{
			switch (state)
			{
				case _DJI DJI_WAYPOINT_V3_ACTION_STATE_IDLE:
					return "空闲"sv;
				case _DJI DJI_WAYPOINT_V3_ACTION_STATE_RUNNING:
					return "正在执行"sv;
				case _DJI DJI_WAYPOINT_V3_ACTION_STATE_FINISHED:
					return "执行完成"sv;
				default:
					return "未知状态"sv;
			}
		}

		// 根据 HMS 错误代码获取错误描述
		inline _STD string_view getHmsErrorDescription(_STD uint32_t errorCode)
		{
			static const auto& hms_error_code_map = []
			{
				_STD unordered_map<_STD uint32_t, const char*> map {};
				const auto&									   size { sizeof(_DJI hmsErrCodeInfoTbl) / sizeof(_DJI T_DjiHmsErrCodeInfo) };
				for (_STD size_t i { 0 }; i < size; ++i)
				{
					map[_DJI hmsErrCodeInfoTbl[i].alarmId] = _DJI hmsErrCodeInfoTbl[i].groundAlarmInfo;
				}
				return map;
			}();

			if (const auto& it { hms_error_code_map.find(errorCode) }; it != hms_error_code_map.end())
			{
				return it->second;
			}
			else
			{
				return "Unknown HMS Error"sv;
			}
		}
	} // namespace

	PSDKAdapter& PSDKAdapter::getInstance(void) noexcept
	{
		static PSDKAdapter instance {};
		return instance;
	}

	// 注册命令事件监听器的模板方法
	template<typename PayloadType, typename Func>
	void PSDKAdapter::registerCommandListener(plane::manager::EventManager::CommandEvent event, Func func)
	{
		this->command_queue_remover_->appendListener(
			event,
			[this, func](const plane::manager::EventManager::CommandEvent& event, const plane::manager::EventManager::CommandData& data)
			{
				// 根据 PayloadType 进行类型匹配和调用
				if constexpr (_STD is_same_v<PayloadType, _STD monostate>)
				{
					if (_STD holds_alternative<_STD monostate>(data))
					{
						LOG_DEBUG("处理无负载命令事件 '{}'", static_cast<int>(event));
						_STD invoke(func, this);
					}
					else
					{
						LOG_ERROR("事件 '{}' 期望一个空的负载 (monostate)，但收到了其他类型！", static_cast<int>(event));
					}
				}
				else
				{
					if (auto* p { _STD get_if<PayloadType>(&data) })
					{
						LOG_DEBUG("处理命令事件 '{}'，负载类型 '{}'", static_cast<int>(event), typeid(PayloadType).name());
						_STD invoke(func, this, *p);
					}
					else
					{
						LOG_ERROR("事件 '{}' 期望负载类型 '{}'，但收到了不匹配的类型！", static_cast<int>(event), typeid(PayloadType).name());
					}
				}
			}
		);
	}

	PSDKAdapter::PSDKAdapter(void) noexcept: command_pool_(_STD make_unique<_BS thread_pool<>>(6))
	{
		LOG_INFO("PSDKAdapter 正在初始化并设置 CommandQueue 的监听器");

		try
		{
			// 订阅 CommandQueue 以接收命令事件
			auto& command_queue_source { plane::manager::EventManager::getInstance().getCommandQueue() };
			this->command_queue_remover_ =
				_STD make_unique<_EVENTPP ScopedRemover<plane::manager::EventManager::CommandQueue>>(command_queue_source);

			// 注册各个命令事件的监听器
			this->registerCommandListener<plane::protocol::TakeoffPayload>(
				plane::manager::EventManager::CommandEvent::Takeoff,
				&PSDKAdapter::takeoffAsync
			);

			this->registerCommandListener<_STD monostate>(plane::manager::EventManager::CommandEvent::GoHome, &PSDKAdapter::goHomeAsync);

			this->registerCommandListener<_STD monostate>(plane::manager::EventManager::CommandEvent::Hover, &PSDKAdapter::hoverAsync);

			this->registerCommandListener<_STD monostate>(plane::manager::EventManager::CommandEvent::Land, &PSDKAdapter::landAsync);

			this->registerCommandListener<_DEFINED _KMZ_DATA_TYPE>(
				plane::manager::EventManager::CommandEvent::WaypointMission,
				&PSDKAdapter::waypointAsync
			);

			this->registerCommandListener<_STD monostate>(
				plane::manager::EventManager::CommandEvent::StopWaypointMission,
				&PSDKAdapter::stopWaypointMissionAsync
			);

			this->registerCommandListener<_STD monostate>(
				plane::manager::EventManager::CommandEvent::PauseWaypointMission,
				&PSDKAdapter::pauseWaypointMissionAsync
			);

			this->registerCommandListener<_STD monostate>(
				plane::manager::EventManager::CommandEvent::ResumeWaypointMission,
				&PSDKAdapter::resumeWaypointMissionAsync
			);

			this->registerCommandListener<plane::protocol::CircleFlyPayload>(
				plane::manager::EventManager::CommandEvent::FlyCircleAroundPoint,
				&PSDKAdapter::selfPOIAsync
			);

			this->registerCommandListener<_DEFINED _PTZ_CONTROL_STRATEGY_TYPE>(
				plane::manager::EventManager::CommandEvent::SetControlStrategy,
				&PSDKAdapter::setControlStrategyAsync
			);

			this->registerCommandListener<plane::protocol::GimbalControlPayload>(
				plane::manager::EventManager::CommandEvent::RotateGimbal,
				&PSDKAdapter::rotateGimbal
			);

			this->registerCommandListener<plane::protocol::GimbalControlPayload>(
				plane::manager::EventManager::CommandEvent::RotateGimbalBySpeed,
				&PSDKAdapter::rotateGimbal
			);

			this->registerCommandListener<plane::protocol::ZoomControlPayload>(
				plane::manager::EventManager::CommandEvent::SetCameraZoomFactor,
				&PSDKAdapter::setCameraZoomFactor
			);

			this->registerCommandListener<_DEFINED _VIDEO_SOURCE_TYPE>(
				plane::manager::EventManager::CommandEvent::SetCameraStreamSource,
				&PSDKAdapter::setCameraStreamSource
			);

			this->registerCommandListener<plane::protocol::StickDataPayload>(
				plane::manager::EventManager::CommandEvent::SendRawStickData,
				&PSDKAdapter::sendRawStickData
			);

			this->registerCommandListener<plane::protocol::StickModeSwitchPayload>(
				plane::manager::EventManager::CommandEvent::EnableVirtualStick,
				&PSDKAdapter::enableVirtualStick
			);

			this->registerCommandListener<plane::protocol::StickModeSwitchPayload>(
				plane::manager::EventManager::CommandEvent::DisableVirtualStick,
				&PSDKAdapter::disableVirtualStick
			);

			this->registerCommandListener<plane::protocol::NedVelocityPayload>(
				plane::manager::EventManager::CommandEvent::SendNedVelocityCommand,
				&PSDKAdapter::sendNedVelocityCommand
			);

			LOG_INFO("所有命令事件监听器已成功注册");
		}
		catch (const _STD exception& e)
		{
			LOG_ERROR("在 PSDKAdapter 构造期间订阅 CommandQueue 失败: {}", e.what());
		}
	}

	PSDKAdapter::~PSDKAdapter(void) noexcept
	{
		try
		{
			LOG_DEBUG("PSDKAdapter 正在析构");
			this->stop();
		}
		catch (const _STD exception& e)
		{
			LOG_ERROR("PSDKAdapter 析构异常: {}", e.what());
		}
		catch (...)
		{
			LOG_ERROR("PSDKAdapter 析构发生未知异常: <non-std exception>");
		}
	}

	bool PSDKAdapter::start(void) noexcept
	{
		try
		{
			// 仅当当前状态为 STOPPED 时才启动服务
			if (_THIS State expected_state { _THIS State::STOPPED };
				!this->state_.compare_exchange_strong(expected_state, _THIS State::STARTING))
			{
				LOG_WARN("PSDKAdapter::start() 被调用，但服务当前状态为 '{}' (非 STOPPED)，已忽略", static_cast<int>(this->state_.load()));
				return this->state_ == _THIS State::RUNNING;
			}
			else
			{
				LOG_DEBUG("PSDKAdapter 状态从 STOPPED 切换到 STARTING");
			}

			LOG_INFO("PSDKAdapter 启动流程开始");

			// 启动数据采集线程
			if (!this->run_acquisition_.exchange(true))
			{
				this->acquisition_thread_ = _STD thread(&PSDKAdapter::acquisitionLoop, this);
				LOG_INFO("PSDK 数据采集线程已启动");
			}
			else
			{
				LOG_WARN("PSDK 数据采集线程已在运行，跳过启动");
			}

			// 启动命令处理线程
			if (!this->run_command_processing_.exchange(true))
			{
				this->command_processing_thread_ = _STD thread(&PSDKAdapter::commandProcessingLoop, this);
				LOG_INFO("PSDK 命令处理线程已启动");
			}
			else
			{
				LOG_WARN("PSDK 命令处理线程已在运行，跳过启动");
			}

			// 更新状态为 RUNNING
			this->state_ = _THIS State::RUNNING;
			LOG_INFO("PSDK 适配器运行时线程已启动");
			return true;
		}
		catch (const _STD exception& e)
		{
			LOG_ERROR("启动 PSDKAdapter 失败: {}", e.what());
		}
		catch (...)
		{
			LOG_ERROR("启动 PSDKAdapter 失败: 捕获到未知异常");
		}

		// 启动失败时，确保状态回滚到 STOPPED
		this->run_acquisition_		  = false;
		this->run_command_processing_ = false;
		this->state_				  = _THIS State::STOPPED;

		if (this->acquisition_thread_.joinable())
		{
			LOG_DEBUG("等待 PSDK 数据采集线程结束");
			this->acquisition_thread_.join();
		}

		if (this->command_processing_thread_.joinable())
		{
			LOG_DEBUG("等待 PSDK 命令处理线程结束");
			this->command_processing_thread_.join();
		}

		return false;
	}

	void PSDKAdapter::stop(_STD_CHRONO milliseconds timeout) noexcept
	{
		// 仅当当前状态为 RUNNING 时才停止服务
		if (_THIS State expected_state { _THIS State::RUNNING }; !this->state_.compare_exchange_strong(expected_state, _THIS State::STOPPING))
		{
			LOG_DEBUG("PSDKAdapter::stop() 被调用，但服务当前未处于 RUNNING 状态，已忽略");
			return;
		}
		else
		{
			LOG_DEBUG("PSDKAdapter 状态从 RUNNING 切换到 STOPPING");
		}

		LOG_INFO("PSDKAdapter 开始停止流程...（超时时间: {}ms）", timeout.count());

		// 停止运行线程
		if (this->run_acquisition_.exchange(false))
		{
			// 等待数据采集线程结束
			if (this->acquisition_thread_.joinable())
			{
				this->acquisition_thread_.join();
				LOG_INFO("PSDK 数据采集线程已停止");
			}
			else
			{
				LOG_WARN("PSDK 数据采集线程不可联接，可能未正确启动");
			}
		}
		else
		{
			LOG_DEBUG("PSDK 数据采集线程未运行，跳过停止");
		}

		// 停止命令处理线程
		if (this->run_command_processing_.exchange(false))
		{
			LOG_DEBUG("PSDK 命令处理线程状态切换为停止");
			plane::manager::EventManager::getInstance().publishCommand(plane::manager::EventManager::CommandEvent::Takeoff, _STD monostate {});

			if (this->command_processing_thread_.joinable())
			{
				this->command_processing_thread_.join();
				LOG_INFO("PSDK 命令处理线程已停止");
			}
			else
			{
				LOG_WARN("PSDK 命令处理线程不可联接，可能未正确启动");
			}
		}
		else
		{
			LOG_DEBUG("PSDK 命令处理线程未运行，跳过停止");
		}

		// 停止命令执行线程池
		if (this->command_pool_)
		{
			// 异步地重置线程池，以避免阻塞当前停止流程
			auto future = _STD async(
				_STD launch::async,
				[this]
				{
					this->command_pool_.reset();
				}
			);

			LOG_INFO("正在等待命令线程池中的任务完成");
			if (future.wait_for(timeout) == _STD future_status::timeout)
			{
				LOG_ERROR("关闭 PSDK 命令线程池超时。可能有一个任务仍在后台运行。stop() 函数将不再等待，继续关闭流程");
			}
			else
			{
				LOG_INFO("PSDK 命令执行线程池已成功关闭");
			}
		}

		LOG_INFO("PSDK 适配器运行时线程已停止");
	}

	plane::protocol::StatusPayload PSDKAdapter::getLatestStatusPayload(void) const noexcept
	{
		// 获取最新的状态负载数据
		_STD lock_guard<_STD mutex> lock(this->payload_mutex_);
		return this->latest_payload_;
	}

	bool PSDKAdapter::subscribeTelemetryData(void) noexcept
	{
		LOG_INFO("正在订阅遥测数据主题");

		// 辅助函数：订阅指定主题并处理错误
		auto subscribe = [&](_DJI E_DjiFcSubscriptionTopic topic, _STD string_view topicName)
		{
			// 订阅主题
			if (_DJI T_DjiReturnCode return_code {
					_DJI DjiFcSubscription_SubscribeTopic(topic, _DJI DJI_DATA_SUBSCRIPTION_TOPIC_10_HZ, nullptr) };
				return_code != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
			{
				LOG_ERROR(
					"订阅主题 '{}' 失败 (飞机不支持?), 错误: {}, 错误码: {:#08x}",
					topicName,
					plane::utils::convertDjiError(return_code),
					return_code
				);

				return false;
			}
			else
			{
				LOG_DEBUG("成功订阅主题 '{}'", topicName);
				return true;
			}
		};

		// 订阅所需的遥测主题
		this->sub_status_.positionFused		  = subscribe(_DJI DJI_FC_SUBSCRIPTION_TOPIC_POSITION_FUSED, "POSITION_FUSED"sv);
		this->sub_status_.altitudeFused		  = subscribe(_DJI DJI_FC_SUBSCRIPTION_TOPIC_ALTITUDE_FUSED, "ALTITUDE_FUSED"sv);
		this->sub_status_.altitudeOfHomepoint = subscribe(_DJI DJI_FC_SUBSCRIPTION_TOPIC_ALTITUDE_OF_HOMEPOINT, "ALTITUDE_OF_HOMEPOINT"sv);
		this->sub_status_.quaternion		  = subscribe(_DJI DJI_FC_SUBSCRIPTION_TOPIC_QUATERNION, "QUATERNION"sv);
		this->sub_status_.velocity			  = subscribe(_DJI DJI_FC_SUBSCRIPTION_TOPIC_VELOCITY, "VELOCITY"sv);
		this->sub_status_.batteryInfo		  = subscribe(_DJI DJI_FC_SUBSCRIPTION_TOPIC_BATTERY_INFO, "BATTERY_INFO"sv);
		this->sub_status_.gimbalAngles		  = subscribe(_DJI DJI_FC_SUBSCRIPTION_TOPIC_GIMBAL_ANGLES, "GIMBAL_ANGLES"sv);
		this->sub_status_.batterySingleInfo =
			subscribe(_DJI DJI_FC_SUBSCRIPTION_TOPIC_BATTERY_SINGLE_INFO_INDEX1, "BATTERY_SINGLE_INFO_INDEX1"sv);
		this->sub_status_.statusFlight		 = subscribe(_DJI DJI_FC_SUBSCRIPTION_TOPIC_STATUS_FLIGHT, "STATUS_FLIGHT"sv);
		this->sub_status_.statusDisplayMode	 = subscribe(_DJI DJI_FC_SUBSCRIPTION_TOPIC_STATUS_DISPLAYMODE, "STATUS_DISPLAYMODE"sv);
		this->sub_status_.homePointInfo		 = subscribe(_DJI DJI_FC_SUBSCRIPTION_TOPIC_HOME_POINT_INFO, "HOME_POINT_INFO"sv);
		this->sub_status_.homePointSetStatus = subscribe(_DJI DJI_FC_SUBSCRIPTION_TOPIC_HOME_POINT_SET_STATUS, "HOME_POINT_SET_STATUS"sv);
		this->sub_status_.gpsSignalLevel	 = subscribe(_DJI DJI_FC_SUBSCRIPTION_TOPIC_GPS_SIGNAL_LEVEL, "GPS_SIGNAL_LEVEL"sv);
		this->sub_status_.gpsControlLevel	 = subscribe(_DJI DJI_FC_SUBSCRIPTION_TOPIC_GPS_CONTROL_LEVEL, "GPS_CONTROL_LEVEL"sv);
		this->sub_status_.controlDevice		 = subscribe(_DJI DJI_FC_SUBSCRIPTION_TOPIC_CONTROL_DEVICE, "CONTROL_DEVICE"sv);

		// 注册 HMS 信息回调
		if (_DJI T_DjiReturnCode return_code { _DJI DjiHmsManager_RegHmsInfoCallback(hmsInfoCallbackEntry) };
			return_code != _DJI	 DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
		{
			LOG_ERROR("注册 HMS 信息回调失败, 错误: {}", plane::utils::convertDjiError(return_code));
		}
		else
		{
			LOG_INFO("成功注册 HMS 信息回调");
		}

		LOG_INFO("PSDK 适配器准备就绪");

		// 读取固定设备信息 (飞控序列号等) 写入域模型 (一次即可, 失败仅告警)
		this->refreshFixedAircraftInfo();

		// 读取相机固定信息 (型号/固件版本) 写入域模型 (一次即可, 失败仅告警)
		this->refreshFixedCameraInfo();

		// 注册返航电量/剩余飞行时间回调 (低电量返航评估; 失败仅告警, 飞机不支持时为预期情况)
		if (_DJI T_DjiReturnCode rc { _DJI DjiFlightController_RegisterBatteryCapacityGohomeCallBack(batteryCapacityGohomeCallbackEntry) };
			rc != _DJI			 DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
		{
			LOG_WARN("注册返航电量回调失败 (飞机不支持?), 错误: {}", plane::utils::convertDjiError(rc));
		}
		else
		{
			LOG_INFO("成功注册返航电量回调");
		}

		return true;
	}

	void PSDKAdapter::refreshFixedAircraftInfo(void) noexcept
	{
		// 从飞控读取 SN
		_DJI T_DjiFlightControllerGeneralInfo gi {};
		if (_DJI DjiFlightController_GetGeneralInfo(&gi) == _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
		{
			_STD string sn { gi.serialNum };
			// serialNum 为定长数组, 去除首尾空白/\0
			_STD size_t begin { 0 };
			while (begin < sn.size() && (sn[begin] == ' ' || sn[begin] == '\0'))
			{
				++begin;
			}
			_STD size_t end { sn.size() };
			while (end > begin && (sn[end - 1] == ' ' || sn[end - 1] == '\0'))
			{
				--end;
			}
			sn = sn.substr(begin, end - begin);

			if (!sn.empty())
			{
				plane::domain::PlaneStateStore::getInstance().update(
					[&sn](plane::domain::PlaneStateDataClass& st)
					{
						st.serial_number		  = sn;
						st.swarm_agent_identifier = _FMT format("swarm.agent.{}", sn);
					}
				);
				LOG_INFO("已从飞控读取序列号: {}", sn);
			}
			else
			{
				LOG_WARN("飞控序列号为空");
			}
		}
		else
		{
			LOG_WARN("读取飞控通用信息(序列号)失败");
		}
	}

	void PSDKAdapter::unsubscribeTelemetryData(void) noexcept
	{
		LOG_INFO("正在清理 PSDK 适配器 (取消已订阅的主题)");

		// 辅助函数：取消订阅指定主题并处理错误
		auto unsubscribe = [&](bool was_subscribed, _DJI E_DjiFcSubscriptionTopic topic, _STD string_view topicName)
		{
			if (was_subscribed)
			{
				// 取消订阅主题
				if (_DJI T_DjiReturnCode return_code { _DJI DjiFcSubscription_UnSubscribeTopic(topic) };
					return_code != _DJI	 DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
				{
					LOG_WARN(
						"取消订阅主题 '{}' 失败, 错误: {}, 错误码: {:#08x}",
						topicName,
						plane::utils::convertDjiError(return_code),
						return_code
					);
				}
				else
				{
					LOG_DEBUG("成功取消订阅主题 '{}'", topicName);
				}
			}
			else
			{
				LOG_DEBUG("主题 '{}' 未订阅，跳过取消订阅", topicName);
			}
		};

		// 取消订阅所有已订阅的遥测主题
		unsubscribe(this->sub_status_.positionFused, _DJI DJI_FC_SUBSCRIPTION_TOPIC_POSITION_FUSED, "POSITION_FUSED"sv);
		unsubscribe(this->sub_status_.altitudeFused, _DJI DJI_FC_SUBSCRIPTION_TOPIC_ALTITUDE_FUSED, "ALTITUDE_FUSED"sv);
		unsubscribe(this->sub_status_.altitudeOfHomepoint, _DJI DJI_FC_SUBSCRIPTION_TOPIC_ALTITUDE_OF_HOMEPOINT, "ALTITUDE_OF_HOMEPOINT"sv);
		unsubscribe(this->sub_status_.quaternion, _DJI DJI_FC_SUBSCRIPTION_TOPIC_QUATERNION, "QUATERNION"sv);
		unsubscribe(this->sub_status_.velocity, _DJI DJI_FC_SUBSCRIPTION_TOPIC_VELOCITY, "VELOCITY"sv);
		unsubscribe(this->sub_status_.batteryInfo, _DJI DJI_FC_SUBSCRIPTION_TOPIC_BATTERY_INFO, "BATTERY_INFO"sv);
		unsubscribe(this->sub_status_.gimbalAngles, _DJI DJI_FC_SUBSCRIPTION_TOPIC_GIMBAL_ANGLES, "GIMBAL_ANGLES"sv);
		unsubscribe(
			this->sub_status_.batterySingleInfo,
			_DJI DJI_FC_SUBSCRIPTION_TOPIC_BATTERY_SINGLE_INFO_INDEX1,
			"BATTERY_SINGLE_INFO_INDEX1"sv
		);
		unsubscribe(this->sub_status_.statusFlight, _DJI DJI_FC_SUBSCRIPTION_TOPIC_STATUS_FLIGHT, "STATUS_FLIGHT"sv);
		unsubscribe(this->sub_status_.statusDisplayMode, _DJI DJI_FC_SUBSCRIPTION_TOPIC_STATUS_DISPLAYMODE, "STATUS_DISPLAYMODE"sv);
		unsubscribe(this->sub_status_.homePointInfo, _DJI DJI_FC_SUBSCRIPTION_TOPIC_HOME_POINT_INFO, "HOME_POINT_INFO"sv);
		unsubscribe(this->sub_status_.homePointSetStatus, _DJI DJI_FC_SUBSCRIPTION_TOPIC_HOME_POINT_SET_STATUS, "HOME_POINT_SET_STATUS"sv);
		unsubscribe(this->sub_status_.gpsSignalLevel, _DJI DJI_FC_SUBSCRIPTION_TOPIC_GPS_SIGNAL_LEVEL, "GPS_SIGNAL_LEVEL"sv);
		unsubscribe(this->sub_status_.gpsControlLevel, _DJI DJI_FC_SUBSCRIPTION_TOPIC_GPS_CONTROL_LEVEL, "GPS_CONTROL_LEVEL"sv);
		unsubscribe(this->sub_status_.controlDevice, _DJI DJI_FC_SUBSCRIPTION_TOPIC_CONTROL_DEVICE, "CONTROL_DEVICE"sv);

		// 注销返航电量回调 (失败忽略)
		if (_DJI T_DjiReturnCode rc { _DJI DjiFlightController_AntiRegisterBatteryCapacityGohomeCallBack() };
			rc != _DJI			 DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
		{
			LOG_DEBUG("注销返航电量回调失败: {}", plane::utils::convertDjiError(rc));
		}
	}

	void PSDKAdapter::
		convertQuaternionToEulerAngle(const _DJI T_DjiFcSubscriptionQuaternion& q, double& roll, double& pitch, double& yaw) noexcept
	{
		// 将四元数转换为欧拉角 (roll, pitch, yaw)，单位为度
		const double sinr_cosp { 2 * (q.q0 * q.q1 + q.q2 * q.q3) };
		const double cosr_cosp { 1 - 2 * (q.q1 * q.q1 + q.q2 * q.q2) };
		roll = _STD atan2(sinr_cosp, cosr_cosp) * 180.0 / _DEFINED MATH_PI;

		const double											   sinp { 2 * (q.q0 * q.q2 - q.q3 * q.q1) };
		if (_STD abs(sinp) >= 1)
		{
			pitch = _STD copysign(_DEFINED MATH_PI / 2, sinp) * 180.0 / _DEFINED MATH_PI;
		}
		else
		{
			pitch = _STD asin(sinp) * 180.0 / _DEFINED MATH_PI;
		}

		const double siny_cosp { 2 * (q.q0 * q.q3 + q.q1 * q.q2) };
		const double cosy_cosp { 1 - 2 * (q.q2 * q.q2 + q.q3 * q.q3) };
		yaw = _STD atan2(siny_cosp, cosy_cosp) * 180.0 / _DEFINED MATH_PI;
	}

	void PSDKAdapter::acquisitionLoop(void) noexcept
	{
		// 数据采集主循环
		while (this->run_acquisition_)
		{
			// 采集数据并构建状态负载
			auto						   start_time { _STD_CHRONO steady_clock::now() };
			plane::protocol::StatusPayload current_payload {};
			_DJI T_DjiDataTimestamp		   timestamp {};

			// 真数据采集的附加量 (无对应上报负载字段, 仅写域模型)
			double battery_temperature_c { 0.0 }; // 主电池温度 (℃)
			int	   gps_signal_level { -1 };		  // GPS 信号等级 (0-5)
			int	   gps_control_level { -1 };	  // GPS 控制等级
			int	   control_authority { -1 };	  // 控制权归属 (PSDK 枚举原值; -1 未知)
			int	   battery_current_ma { 0 };	  // 主电池电流 (mA)
			int	   battery_cell_count { 0 };	  // 电芯个数
			int	   flight_status_code { -1 };	  // STATUS_FLIGHT (0停桨/1地面转/2空中)
			int	   display_mode_code { -1 };	  // STATUS_DISPLAYMODE
			double home_latitude_deg { 0.0 };	  // 返航点纬度 (度)
			double home_longitude_deg { 0.0 };	  // 返航点经度 (度)
			double home_altitude_m { 0.0 };		  // 返航点海拔 (m)
			bool   home_location_set { false };	  // 返航点是否已设置

			if (_DJI T_DjiFcSubscriptionPositionFused pos {};
				this->sub_status_.positionFused && (_DJI	  DjiFcSubscription_GetLatestValueOfTopic(
														_DJI DJI_FC_SUBSCRIPTION_TOPIC_POSITION_FUSED,
														(_STD uint8_t*)&pos,
														sizeof(pos),
														&timestamp
													) == _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS))
			{
				current_payload.SXZT.GPSSXSL = pos.visibleSatelliteNumber;
				current_payload.DQJD		 = pos.longitude * _DEFINED RAD_TO_DEG; // 当前经度 (度)
				current_payload.DQWD		 = pos.latitude * _DEFINED	RAD_TO_DEG; // 当前纬度 (度)
				current_payload.JDGD		 = pos.altitude;						// 海拔高度
				// TODO: 根据 pos.gnssFixStatus 和 pos.gpsFixStatus 来填充 SFSL 和 SXDW
			}

			if (_DJI T_DjiFcSubscriptionAltitudeFused fused_alt {};
				this->sub_status_.altitudeFused && (_DJI	  DjiFcSubscription_GetLatestValueOfTopic(
														_DJI DJI_FC_SUBSCRIPTION_TOPIC_ALTITUDE_FUSED,
														(_STD uint8_t*)&fused_alt,
														sizeof(fused_alt),
														&timestamp
													) == _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS))
			{
				if (_DJI T_DjiFcSubscriptionAltitudeFused hp_alt {};
					this->sub_status_.altitudeOfHomepoint && (_DJI		DjiFcSubscription_GetLatestValueOfTopic(
																  _DJI DJI_FC_SUBSCRIPTION_TOPIC_ALTITUDE_OF_HOMEPOINT,
																  (_STD uint8_t*)&hp_alt,
																  sizeof(hp_alt),
																  &timestamp
															  ) == _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS))
				{
					current_payload.XDQFGD = fused_alt - hp_alt; // 相对起飞点高度
					current_payload.JHB	   = hp_alt;			 // Home 点海拔
					home_altitude_m		   = hp_alt;			 // 域模型返航点海拔
				}
			}

			if (_DJI T_DjiFcSubscriptionQuaternion q {}; this->sub_status_.quaternion && (_DJI		DjiFcSubscription_GetLatestValueOfTopic(
																							  _DJI DJI_FC_SUBSCRIPTION_TOPIC_QUATERNION,
																							  (_STD uint8_t*)&q,
																							  sizeof(q),
																							  &timestamp
																						  ) == _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS))
			{
				this->convertQuaternionToEulerAngle(q, current_payload.FJHGJ, current_payload.FJFYJ, current_payload.FJPHJ);
			}

			if (_DJI T_DjiFcSubscriptionVelocity vel {}; this->sub_status_.velocity && (_DJI	  DjiFcSubscription_GetLatestValueOfTopic(
																							_DJI DJI_FC_SUBSCRIPTION_TOPIC_VELOCITY,
																							(_STD uint8_t*)&vel,
																							sizeof(vel),
																							&timestamp
																						) == _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS))
			{
				current_payload.VY	 = vel.data.x;	// 北向速度 (North)
				current_payload.VX	 = vel.data.y;	// 东向速度 (East)
				current_payload.VZ	 = -vel.data.z; // 地向速度 (Down). PSDK z 轴向上为正, 我们的协议下为正, 所以取反
				current_payload.SPSD = _STD sqrt(vel.data.x * vel.data.x + vel.data.y * vel.data.y); // 水平速度
				current_payload.CZSD = vel.data.z;													 // 垂直速度
			}

			// 整机电池信息 (PSDK 结构为 T_DjiFcSubscriptionWholeBatteryInfo)
			if (_DJI T_DjiFcSubscriptionWholeBatteryInfo batt {};
				this->sub_status_.batteryInfo && (_DJI		DjiFcSubscription_GetLatestValueOfTopic(
													  _DJI DJI_FC_SUBSCRIPTION_TOPIC_BATTERY_INFO,
													  (_STD uint8_t*)&batt,
													  sizeof(batt),
													  &timestamp
												  ) == _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS))
			{
				current_payload.DCXX.SYDL = batt.percentage;
				current_payload.DCXX.ZDY  = batt.voltage;
			}

			// 主电池单电池详情 (INDEX1): 温度/电流/电芯数 (写域模型)
			if (_DJI T_DjiFcSubscriptionSingleBatteryInfo single {};
				this->sub_status_.batterySingleInfo && (_DJI	  DjiFcSubscription_GetLatestValueOfTopic(
															_DJI DJI_FC_SUBSCRIPTION_TOPIC_BATTERY_SINGLE_INFO_INDEX1,
															(_STD uint8_t*)&single,
															sizeof(single),
															&timestamp
														) == _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS))
			{
				battery_temperature_c = single.batteryTemperature * 0.1; // 0.1℃ -> ℃
				battery_current_ma	  = single.currentElectric;
				battery_cell_count	  = single.cellCount;
			}

			if (_DJI T_DjiFcSubscriptionGimbalAngles gimbal_angle {};
				this->sub_status_.gimbalAngles && (_DJI		 DjiFcSubscription_GetLatestValueOfTopic(
													   _DJI DJI_FC_SUBSCRIPTION_TOPIC_GIMBAL_ANGLES,
													   (_STD uint8_t*)&gimbal_angle,
													   sizeof(gimbal_angle),
													   &timestamp
												   ) == _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS))
			{
				current_payload.YTFY = gimbal_angle.x;
				current_payload.YTHG = gimbal_angle.y;
				current_payload.YTPH = gimbal_angle.z;
			}

			// 飞行状态 / 显示模式 (真数据)
			if (_DJI E_DjiFcSubscriptionFlightStatus flight_status {};
				this->sub_status_.statusFlight && (_DJI		 DjiFcSubscription_GetLatestValueOfTopic(
													   _DJI DJI_FC_SUBSCRIPTION_TOPIC_STATUS_FLIGHT,
													   (_STD uint8_t*)&flight_status,
													   sizeof(flight_status),
													   &timestamp
												   ) == _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS))
			{
				flight_status_code = static_cast<int>(flight_status);
			}

			if (_DJI E_DjiFcSubscriptionDisplayMode display_mode {};
				this->sub_status_.statusDisplayMode && (_DJI	  DjiFcSubscription_GetLatestValueOfTopic(
															_DJI DJI_FC_SUBSCRIPTION_TOPIC_STATUS_DISPLAYMODE,
															(_STD uint8_t*)&display_mode,
															sizeof(display_mode),
															&timestamp
														) == _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS))
			{
				display_mode_code = static_cast<int>(display_mode);
			}

			// 返航点
			if (_DJI T_DjiFcSubscriptionHomePointInfo home {};
				this->sub_status_.homePointInfo && (_DJI	  DjiFcSubscription_GetLatestValueOfTopic(
														_DJI DJI_FC_SUBSCRIPTION_TOPIC_HOME_POINT_INFO,
														(_STD uint8_t*)&home,
														sizeof(home),
														&timestamp
													) == _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS))
			{
				home_latitude_deg	= home.latitude * _DEFINED	 RAD_TO_DEG;
				home_longitude_deg	= home.longitude * _DEFINED RAD_TO_DEG;
				current_payload.JJD = home_latitude_deg;
				current_payload.JWD = home_longitude_deg;
			}

			if (_DJI T_DjiFcSubscriptionHomePointSetStatus home_set {};
				this->sub_status_.homePointSetStatus && (_DJI	   DjiFcSubscription_GetLatestValueOfTopic(
															 _DJI DJI_FC_SUBSCRIPTION_TOPIC_HOME_POINT_SET_STATUS,
															 (_STD uint8_t*)&home_set,
															 sizeof(home_set),
															 &timestamp
														 ) == _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS))
			{
				home_location_set = (home_set == _DJI DJI_FC_SUBSCRIPTION_HOME_POINT_SET_STATUS_SUCCESS);
			}

			// GPS 信号等级 / GPS 控制等级 (SBZT: SFSL / SXDW)
			if (_DJI T_DjiFcSubscriptionGpsSignalLevel gps_signal {};
				this->sub_status_.gpsSignalLevel && (_DJI	   DjiFcSubscription_GetLatestValueOfTopic(
														 _DJI DJI_FC_SUBSCRIPTION_TOPIC_GPS_SIGNAL_LEVEL,
														 (_STD uint8_t*)&gps_signal,
														 sizeof(gps_signal),
														 &timestamp
													 ) == _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS))
			{
				gps_signal_level		  = static_cast<int>(gps_signal);
				current_payload.SXZT.SFSL = gps_signal_level; // 锁星水平 (信号等级 0-5)
			}

			if (_DJI T_DjiFcSubscriptionGpsControlLevel gps_control {};
				this->sub_status_.gpsControlLevel && (_DJI		DjiFcSubscription_GetLatestValueOfTopic(
														  _DJI DJI_FC_SUBSCRIPTION_TOPIC_GPS_CONTROL_LEVEL,
														  (_STD uint8_t*)&gps_control,
														  sizeof(gps_control),
														  &timestamp
													  ) == _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS))
			{
				gps_control_level		  = static_cast<int>(gps_control);
				current_payload.SXZT.SXDW = gps_control_level; // 锁星定位 (控制等级原值)
			}

			// 控制设备 (控制权归属; 0 RC / 1 MSDK / 4 PSDK / 5 Dock)
			if (_DJI T_DjiFcSubscriptionControlDevice control_device {};
				this->sub_status_.controlDevice && (_DJI	  DjiFcSubscription_GetLatestValueOfTopic(
														_DJI DJI_FC_SUBSCRIPTION_TOPIC_CONTROL_DEVICE,
														(_STD uint8_t*)&control_device,
														sizeof(control_device),
														&timestamp
													) == _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS))
			{
				control_authority = static_cast<int>(control_device.controlAuthority);
			}

			// 激光测距 (1s 节流轮询; 相机不支持/无激光时静默保持上次值)
			if (const auto now_laser { _STD_CHRONO steady_clock::now() }; now_laser - this->last_laser_poll_ >= _STD_CHRONO seconds(1))
			{
				this->last_laser_poll_ = now_laser;

				_DJI T_DjiCameraManagerLaserRangingInfo laser {};
				if (_DJI T_DjiReturnCode rc { _DJI DjiCameraManager_GetLaserRangingInfo(_DJI DJI_MOUNT_POSITION_PAYLOAD_PORT_NO1, &laser) };
					rc == _DJI			 DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
				{
					this->laser_distance_01m_.store(laser.distance, _STD memory_order_release); // 0.1m 单位
				}
				else
				{
					LOG_DEBUG("激光测距读取失败 (无相机/不支持): {}", plane::utils::convertDjiError(rc));
				}
			}

			// 激光距离 -> SBZT 的 JGCJ (米; 未测到保持 0)
			const int laser_raw_distance { this->laser_distance_01m_.load(_STD memory_order_acquire) };
			if (laser_raw_distance >= 0)
			{
				current_payload.JGCJ = static_cast<double>(laser_raw_distance) * 0.1;
			}

			// 航点进度 (来自航线任务回调; ZHD 暂无 PSDK 来源, 保持 0)
			current_payload.DQHD = this->mission_current_waypoint_.load(_STD memory_order_acquire);

			// 虚拟摇杆状态 (来自 Enable/DisableVirtualStick 命令: YGMS 0 关 / 1 启用 / 2 高级)
			if (const int vs_mode { this->virtual_stick_mode_.load(_STD memory_order_acquire) }; vs_mode > 0)
			{
				current_payload.VSE = 1;
				current_payload.AME = (vs_mode == 2) ? 1 : 0;
			}

			if (_DJI T_DjiAircraftInfoBaseInfo aircraft_info {};
				_DJI DjiAircraftInfo_GetBaseInfo(&aircraft_info) == _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
			{
				current_payload.XH = _UNNAMED aircraftTypeToString(aircraft_info.aircraftType);
			}
			else
			{
				current_payload.XH = "N/A";
			}

			current_payload.CJ = "DJI";

			// STATUS_DISPLAYMODE -> 上报 MODE (中文, 尽量对齐 msdk 语义; 未映射值回退原始码)
			if (display_mode_code >= 0)
			{
				switch (display_mode_code)
				{
					case 0:
						current_payload.MODE = "手动模式";
						break;
					case 1:
						current_payload.MODE = "姿态模式";
						break;
					case 6:
						current_payload.MODE = "GPS 普通模式";
						break;
					case 9:
						current_payload.MODE = "兴趣点环绕";
						break;
					case 10:
					case 11:
						current_payload.MODE = "自主起飞";
						break;
					case 12:
						current_payload.MODE = "自动降落";
						break;
					case 15:
						current_payload.MODE = "返航";
						break;
					case 17:
						current_payload.MODE = "程序控制";
						break;
					case 33:
						current_payload.MODE = "强制降落";
						break;
					case 40:
						current_payload.MODE = "搜索模式";
						break;
					case 41:
						current_payload.MODE = "电机已起转";
						break;
					default:
						current_payload.MODE = _FMT format("模式{}", display_mode_code);
						break;
				}
			}

			{
				_STD lock_guard<_STD mutex> lock(this->payload_mutex_);
				this->latest_payload_ = current_payload;
			}

			// 就地更新内部域模型 PlaneStateDataClass (只覆盖本采集器负责的字段;
			// catalog/mqtt/ws/绑定/序列号等由各自模块经 mutator 写入, 避免整份覆盖清零)
			plane::domain::PlaneStateStore::getInstance().update(
				[&](plane::domain::PlaneStateDataClass& ps)
				{
					ps.plane_location_3d.latitude	  = current_payload.DQWD;	// 纬度 (度)
					ps.plane_location_3d.longitude	  = current_payload.DQJD;	// 经度 (度)
					ps.plane_location_3d.altitude	  = current_payload.XDQFGD; // 相对高度
					ps.abs_height					  = current_payload.JDGD;
					ps.gps_satellite_count			  = current_payload.SXZT.GPSSXSL;
					ps.aircraft_velocity_3d.x		  = current_payload.VY;	  // NED x 北向
					ps.aircraft_velocity_3d.y		  = current_payload.VX;	  // NED y 东向
					ps.aircraft_velocity_3d.z		  = current_payload.VZ;	  // NED z 地向 (下为正)
					ps.aircraft_velocity_flight_speed = current_payload.SPSD; // 水平合速度
					ps.aircraft_attitude.pitch		  = current_payload.FJFYJ;
					ps.aircraft_attitude.roll		  = current_payload.FJHGJ;
					ps.aircraft_attitude.yaw		  = current_payload.FJPHJ;
					ps.aircraft_battery_power_percent = current_payload.DCXX.SYDL;
					ps.aircraft_battery_voltage		  = current_payload.DCXX.ZDY;
					ps.gimbal_attitude.pitch		  = current_payload.YTFY;
					ps.gimbal_attitude.roll			  = current_payload.YTHG;
					ps.gimbal_attitude.yaw			  = current_payload.YTPH;
					ps.battery_temperature			  = battery_temperature_c;
					ps.battery_current				  = battery_current_ma;
					ps.battery_number_of_cells		  = battery_cell_count;
					ps.flight_mode					  = displayModeToFlightMode(display_mode_code);			  // 显示模式 -> 域模型枚举
					ps.airlink_flying				  = (flight_status_code == 2);							  // IN_AIR
					ps.are_motors_on				  = (flight_status_code == 1 || flight_status_code == 2); // 地面转/空中
					ps.home_location.latitude		  = home_latitude_deg;
					ps.home_location.longitude		  = home_longitude_deg;
					ps.home_location_altitude		  = home_altitude_m;
					ps.home_location_set			  = home_location_set;
					ps.is_home_location_set			  = home_location_set;

					// 激光测距距离 (测到时更新)
					if (laser_raw_distance >= 0)
					{
						ps.laser_measure_information.distance = current_payload.JGCJ;
					}

					// 虚拟摇杆状态 (命令驱动的本机状态)
					ps.virtual_stick_state.is_virtual_stick_enable				  = (current_payload.VSE == 1);
					ps.virtual_stick_state.is_virtual_stick_advanced_mode_enabled = (current_payload.AME == 1);

					// 控制权归属 (CONTROL_DEVICE: 0 RC / 1 MSDK / 4 PSDK / 5 Dock)
					switch (control_authority)
					{
						case 0:
							ps.virtual_stick_state.current_control_permission_owner = plane::domain::FlightControlAuthority::RC;
							break;
						case 1:
							ps.virtual_stick_state.current_control_permission_owner = plane::domain::FlightControlAuthority::MSDK;
							break;
						case 4:
							ps.virtual_stick_state.current_control_permission_owner = plane::domain::FlightControlAuthority::PSDK;
							break;
						case 5:
							ps.virtual_stick_state.current_control_permission_owner = plane::domain::FlightControlAuthority::DOCK;
							break;
						default:
							break;
					}
				}
			);

			// 发布更新的状态负载事件
			plane::manager::EventManager::getInstance()
				.publishStatus(plane::manager::EventManager::PSDKEvent::TelemetryUpdated, current_payload);

			// 发布健康状态心跳事件
			plane::manager::EventManager::getInstance().publishStatus(EventManager::PSDKEvent::HealthPing, _STD_CHRONO steady_clock::now());

			// 控制采集频率
			auto end_time { _STD_CHRONO steady_clock::now() };
			if (auto elapsed_time { _STD_CHRONO duration_cast<_STD_CHRONO milliseconds>(end_time - start_time) };
				elapsed_time < this->ACQUISITION_INTERVAL)
			{
				_STD this_thread::sleep_for(this->ACQUISITION_INTERVAL - elapsed_time);
			}
		}
	}

	void PSDKAdapter::missionStateCallback(_DJI T_DjiWaypointV3MissionState missionState)
	{
		// 处理航线任务状态回调
		LOG_INFO(
			"[航线任务状态] 状态: {}, 当前航点: {}, 航线ID: {}",
			_UNNAMED djiMissionStateToString(missionState.state),
			missionState.currentWaypointIndex,
			missionState.wayLineId
		);

		// 记录当前航点 (供采集循环填充 SBZT 的 DQHD)
		this->mission_current_waypoint_.store(static_cast<int>(missionState.currentWaypointIndex), _STD memory_order_release);

		plane::manager::EventManager::getInstance().publishStatus(plane::manager::EventManager::PSDKEvent::MissionStateChanged, missionState);
	}

	_DJI T_DjiReturnCode PSDKAdapter::missionStateCallbackEntry(_DJI T_DjiWaypointV3MissionState missionState)
	{
		PSDKAdapter::getInstance().missionStateCallback(missionState);
		return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
	}

	void PSDKAdapter::actionStateCallback(_DJI T_DjiWaypointV3ActionState actionState)
	{
		// 处理航线任务动作状态回调
		LOG_INFO(
			"[航线动作状态] 状态: {}, 航点: {}, 动作组: {}, 动作ID: {}",
			_UNNAMED djiActionStateToString(actionState.state),
			actionState.currentWaypointIndex,
			actionState.actionGroupId,
			actionState.actionId
		);

		plane::manager::EventManager::getInstance().publishStatus(plane::manager::EventManager::PSDKEvent::ActionStateChanged, actionState);
	}

	_DJI T_DjiReturnCode PSDKAdapter::actionStateCallbackEntry(_DJI T_DjiWaypointV3ActionState actionState)
	{
		PSDKAdapter::getInstance().actionStateCallback(actionState);
		return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
	}

	void PSDKAdapter::hmsInfoCallback(_DJI T_DjiHmsInfoTable hmsInfoTable)
	{
		// 处理 HMS 信息回调
		_STD vector<_STD uint32_t> current_error_codes {};
		if (hmsInfoTable.hmsInfoNum > 0)
		{
			current_error_codes.reserve(hmsInfoTable.hmsInfoNum);
			for (_STD uint32_t i { 0 }; i < hmsInfoTable.hmsInfoNum; ++i)
			{
				current_error_codes.push_back(hmsInfoTable.hmsInfo[i].errorCode);
			}
		}
		else
		{
			LOG_DEBUG("收到 HMS 信息回调，但当前无告警");
		}

		_STD sort(current_error_codes.begin(), current_error_codes.end());

		// 检查告警状态是否有变化
		_STD atomic<bool> status_changed { false };
		{
			_STD lock_guard<_STD mutex> lock(this->hms_mutex_);

			// 比较当前告警代码与上次记录的告警代码
			if (current_error_codes != this->last_hms_error_codes_)
			{
				// 告警状态发生变化，更新状态标志和记录
				status_changed.store(true);
				this->last_hms_error_codes_ = current_error_codes;
			}
		}

		// 如果告警状态有变化，则发布健康状态更新事件
		if (status_changed.load())
		{
			if (hmsInfoTable.hmsInfoNum > 0)
			{
				LOG_INFO("HMS 告警状态发生变化，当前有 {} 条告警，正在上报", hmsInfoTable.hmsInfoNum);
			}
			else
			{
				LOG_INFO("HMS 告警已全部清除，正在上报空列表");
			}

			plane::protocol::HealthStatusPayload health_status {};
			health_status.GJLB.reserve(hmsInfoTable.hmsInfoNum);

			for (_STD uint32_t i { 0 }; i < hmsInfoTable.hmsInfoNum; ++i)
			{
				const auto&							dji_alert { hmsInfoTable.hmsInfo[i] };
				plane::protocol::HealthAlertPayload our_alert {};
				our_alert.GJDJ = dji_alert.errorLevel;
				our_alert.GJMK = dji_alert.componentIndex;
				our_alert.GJM  = _FMT	   format("0x{:08X}", dji_alert.errorCode);
				our_alert.GJBT = _UNNAMED getHmsErrorDescription(dji_alert.errorCode);
				our_alert.GJMS = our_alert.GJBT;

				health_status.GJLB.push_back(our_alert);
			}

			plane::manager::EventManager::getInstance().publishStatus(EventManager::PSDKEvent::HealthStatusUpdated, health_status);
		}
	}

	_DJI T_DjiReturnCode PSDKAdapter::hmsInfoCallbackEntry(_DJI T_DjiHmsInfoTable hmsInfoTable)
	{
		PSDKAdapter::getInstance().hmsInfoCallback(hmsInfoTable);
		return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
	}

	void PSDKAdapter::batteryCapacityGohomeCallback(_DJI T_DjiFlightControllerBatteryCapacityGohome info)
	{
		plane::domain::PlaneStateStore::getInstance().update(
			[&info](plane::domain::PlaneStateDataClass& st)
			{
				if (info.remain_fly_time != 0)
				{
					st.low_battery_rth_info.remaining_flight_time = static_cast<int>(info.remain_fly_time);
				}
				if (info.gohome_capacity != 0)
				{
					st.low_battery_rth_info.battery_percent_needed_to_go_home = static_cast<int>(info.gohome_capacity);
				}
			}
		);
	}

	_DJI T_DjiReturnCode PSDKAdapter::batteryCapacityGohomeCallbackEntry(_DJI T_DjiFlightControllerBatteryCapacityGohome info)
	{
		PSDKAdapter::getInstance().batteryCapacityGohomeCallback(info);
		return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
	}

	void PSDKAdapter::refreshFixedCameraInfo(void) noexcept
	{
		_DJI T_DjiCameraManagerFirmwareVersion firmware {};
		if (_DJI T_DjiReturnCode rc { _DJI DjiCameraManager_GetFirmwareVersion(_DJI DJI_MOUNT_POSITION_PAYLOAD_PORT_NO1, &firmware) };
			rc != _DJI			 DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
		{
			LOG_WARN("读取相机固件版本失败 (无相机或不支持?), 错误: {}", plane::utils::convertDjiError(rc));
			return;
		}

		const _STD string version { _FMT format(
			"{}.{}.{}.{}",
			firmware.firmware_version[0],
			firmware.firmware_version[1],
			firmware.firmware_version[2],
			firmware.firmware_version[3]
		) };
		plane::domain::PlaneStateStore::getInstance().update(
			[&version](plane::domain::PlaneStateDataClass& st)
			{
				st.camera_firmware_version = version;
			}
		);
		LOG_INFO("已从相机读取固件版本: {}", version);
	}

	template<typename CommandLogic>
	_STD future<_DJI T_DjiReturnCode> PSDKAdapter::executePsdkCommandAsync(CommandLogic&& logic, const _STD source_location& location)
	{
		const char* command_name { location.function_name() };

		return this->command_pool_->submit_task(
			[this, name = _STD string(command_name), logic = _STD forward<CommandLogic>(logic)](void) -> _DJI T_DjiReturnCode
			{
				try
				{
					_STD lock_guard<_STD mutex> lock(this->psdk_command_mutex_);

					if (!plane::config::ConfigManager::getInstance().isStandardProceduresEnabled())
					{
						LOG_WARN("没有启用 PSDK 标准作业流程, 命令 '{}' 被禁止", name);
						return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_NONSUPPORT;
					}

					return logic();
				}
				catch (const _STD exception& e)
				{
					LOG_ERROR("PSDKAdapter 命令 '{}' 在线程池中捕获到标准异常: {}", name, e.what());
					return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_UNKNOWN;
				}
				catch (...)
				{
					LOG_ERROR("PSDKAdapter 命令 '{}' 在线程池中捕获到未知异常！", name);
					return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_UNKNOWN;
				}
			}
		);
	}

	_STD future<_DJI T_DjiReturnCode>
		 PSDKAdapter::executeWaypointActionAsync(_DJI E_DjiWaypointV3Action action, const _STD source_location& location)
	{
		const char* command_name { location.function_name() };

		return this->executePsdkCommandAsync(
			[action, name = _STD string(command_name)](void) -> _DJI T_DjiReturnCode
			{
				LOG_INFO("线程池任务: 发送航线动作 '{}'", name);
				_DJI T_DjiReturnCode return_code { _DJI DjiWaypointV3_Action(action) };
				if (return_code != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
				{
					LOG_ERROR("发送航线动作 '{}' 失败, 错误: {}", name, plane::utils::convertDjiError(return_code));
				}

				return return_code;
			},
			location
		);
	}

	_STD future<_DJI T_DjiReturnCode> PSDKAdapter::takeoffAsync(const plane::protocol::TakeoffPayload& takeoffParams)
	{
		LOG_INFO("收到起飞请求，起飞高度: {} 米", takeoffParams.MBGD.value_or(-1.0));

		return this->executePsdkCommandAsync(
			[](void) -> _DJI T_DjiReturnCode
			{
				LOG_INFO("线程池任务: 执行起飞");
				_DJI T_DjiReturnCode return_code { _DJI DjiFlightController_StartTakeoff() };
				if (return_code != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
				{
					LOG_ERROR("起飞失败, 错误: {}", plane::utils::convertDjiError(return_code));
				}

				return return_code;
			}
		);
	}

	_STD future<_DJI T_DjiReturnCode> PSDKAdapter::goHomeAsync(void)
	{
		LOG_INFO("收到返航请求");

		return this->executePsdkCommandAsync(
			[](void) -> _DJI T_DjiReturnCode
			{
				LOG_INFO("线程池任务: 执行返航");
				_DJI T_DjiReturnCode return_code { _DJI DjiFlightController_StartGoHome() };
				if (return_code != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
				{
					LOG_ERROR("返航失败, 错误: {}", plane::utils::convertDjiError(return_code));
				}

				return return_code;
			}
		);
	}

	_STD future<_DJI T_DjiReturnCode> PSDKAdapter::hoverAsync(void)
	{
		LOG_INFO("收到一键悬停请求");

		return this->executePsdkCommandAsync(
			[](void) -> _DJI T_DjiReturnCode
			{
				LOG_INFO("线程池任务: 执行一键悬停");
				_DJI T_DjiReturnCode return_code { _DJI DjiFlightController_ExecuteEmergencyBrakeAction() };
				if (return_code != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
				{
					LOG_ERROR("一键悬停失败, 错误: {}", plane::utils::convertDjiError(return_code));
				}

				return return_code;
			}
		);
	}

	_STD future<_DJI T_DjiReturnCode> PSDKAdapter::landAsync(void)
	{
		LOG_INFO("收到降落请求");

		return this->executePsdkCommandAsync(
			[](void) -> _DJI T_DjiReturnCode
			{
				LOG_INFO("线程池任务: 执行降落");
				_DJI T_DjiReturnCode return_code { _DJI DjiFlightController_StartLanding() };
				if (return_code != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
				{
					LOG_ERROR("降落失败, 错误: {}", plane::utils::convertDjiError(return_code));
				}

				return return_code;
			}
		);
	}

	_STD future<_DJI T_DjiReturnCode> PSDKAdapter::waypointAsync(const _DEFINED _KMZ_DATA_TYPE& kmzData)
	{
		LOG_INFO("收到航线任务请求，KMZ 数据大小: {} 字节", kmzData.size());

		// 专门起一个线程来处理航线任务，避免阻塞线程池中的其他任务
		return _STD async(
			_STD								 launch::async,
			[this, data = kmzData](void) -> _DJI T_DjiReturnCode
			{
				_DJI T_DjiReturnCode return_code { _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS };

				// 如果当前状态不是 RUNNING，则拒绝执行航线任务
				if (this->state_ != _THIS State::RUNNING)
				{
					LOG_WARN("PSDKAdapter 当前未处于 RUNNING 状态，航线任务请求被拒绝");
					return _DJI DJI_ERROR_WAYPOINT_V3_MODULE_CODE_USER_EXIT;
				}

				// 如果 KMZ 数据为空，则返回错误
				if (data.empty())
				{
					LOG_ERROR("提供的 KMZ 数据为空，无法执行航线任务");
					return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
				}

				// 加锁初始化 Promise
				{
					_STD unique_lock<_STD mutex>			 lock(this->psdk_command_mutex_);
					this->mission_completion_promise_ = _STD make_unique<_STD promise<_DJI T_DjiReturnCode>>();
					this->last_mission_state_		  = {};
				}

				// 获取 Future 用于等待
				_STD future<_DJI T_DjiReturnCode> mission_future { this->mission_completion_promise_->get_future() };

				// 初始化 Waypoint V3 模块
				LOG_DEBUG("正在初始化 Waypoint V3 模块");
				if (return_code = _DJI DjiWaypointV3_Init(); return_code != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
				{
					LOG_ERROR("Waypoint V3 初始化失败: {}", plane::utils::convertDjiError(return_code));
					return return_code;
				}

				try
				{
					// 注册任务状态回调
					if (return_code = _DJI	DjiWaypointV3_RegMissionStateCallback(this->missionStateCallbackEntry);
						return_code != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
					{
						LOG_ERROR("注册航线任务状态回调失败: {}", plane::utils::convertDjiError(return_code));
						throw return_code;
					}

					// 注册动作状态回调
					if (return_code = _DJI	DjiWaypointV3_RegActionStateCallback(this->actionStateCallbackEntry);
						return_code != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
					{
						LOG_ERROR("注册航线动作状态回调失败: {}", plane::utils::convertDjiError(return_code));
						throw return_code;
					}

					// 上传 KMZ 数据
					LOG_INFO("正在上传 KMZ 数据");
					if (return_code = _DJI	DjiWaypointV3_UploadKmzFile(data.data(), data.size());
						return_code != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
					{
						LOG_ERROR("上传 KMZ 数据失败: {}", plane::utils::convertDjiError(return_code));
						throw return_code;
					}

					// 启动航线任务
					LOG_INFO("启动航线任务");
					if (return_code = _DJI	DjiWaypointV3_Action(_DJI DJI_WAYPOINT_V3_ACTION_START);
						return_code != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
					{
						LOG_ERROR("启动航线任务失败: {}", plane::utils::convertDjiError(return_code));
						throw return_code;
					}

					// 阻塞等待任务完成, 等待回调函数通知 "IDLE" 或 "FINISHED" , 至多等待 60 分钟
					LOG_INFO("航线任务已启动，等待完成");
					if (_STD future_status status { mission_future.wait_for(_STD_CHRONO minutes(60)) }; status == _STD future_status::ready)
					{
						return_code = mission_future.get();
						LOG_INFO("航线任务结束 (回调确认: {})", plane::utils::convertDjiError(return_code));
					}
					else
					{
						LOG_ERROR("航线任务超时或异常！尝试发送停止指令");
						_DJI			   DjiWaypointV3_Action(_DJI DJI_WAYPOINT_V3_ACTION_STOP);
						return_code = _DJI DJI_ERROR_SYSTEM_MODULE_CODE_TIMEOUT;
					}

					// 注销回调函数
					_DJI DjiWaypointV3_RegMissionStateCallback(nullptr);
					_DJI DjiWaypointV3_RegActionStateCallback(nullptr);

					_STD this_thread::sleep_for(_STD_CHRONO milliseconds(500));

					LOG_INFO("正在反初始化 Waypoint V3 模块");
					_DJI DjiWaypointV3_DeInit();
				}
				catch (_DJI T_DjiReturnCode err_code)
				{
					LOG_ERROR("航线启动流程发生错误: {}", plane::utils::convertDjiError(err_code));
					_DJI DjiWaypointV3_RegMissionStateCallback(nullptr);
					_DJI DjiWaypointV3_RegActionStateCallback(nullptr);
					_DJI DjiWaypointV3_DeInit();
					return err_code;
				}
				catch (...)
				{
					LOG_ERROR("捕获到未知异常，强制清理");
					_DJI		DjiWaypointV3_DeInit();
					return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_UNKNOWN;
				}

				// 清理 Promise
				{
					_STD lock_guard<_STD mutex> re_lock(this->psdk_command_mutex_);
					this->mission_completion_promise_.reset();
				}

				return return_code;
			}
		);
	}

	_STD future<_DJI T_DjiReturnCode> PSDKAdapter::setControlStrategyAsync(const _DEFINED _PTZ_CONTROL_STRATEGY_TYPE& strategyCode)
	{
		LOG_INFO("收到设置控制策略请求，代码: {}", strategyCode);

		return this->executePsdkCommandAsync(
			[strategyCode](void) -> _DJI T_DjiReturnCode
			{
				LOG_INFO("线程池任务: 设置控制策略, 代码: {}", strategyCode);
				_DJI T_DjiReturnCode return_code = _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS; // 假设成功
				if (return_code != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
				{
					LOG_ERROR("设置控制策略失败, 错误: {}", plane::utils::convertDjiError(return_code));
				}

				return return_code;
			}
		);
	}

	_STD future<_DJI T_DjiReturnCode> PSDKAdapter::selfPOIAsync(const plane::protocol::CircleFlyPayload& circleParams)
	{
		LOG_INFO(
			"收到环绕飞行请求, 经度: {}, 纬度: {}, 高度: {}, 速度: {}, 半径: {}, 圈数: {}",
			circleParams.JD,
			circleParams.WD,
			circleParams.GD,
			circleParams.SD,
			circleParams.BJ,
			circleParams.QS
		);

		return this->executePsdkCommandAsync(
			[circleParams](void) -> _DJI T_DjiReturnCode
			{
				LOG_INFO(
					"线程池任务: 执行环绕飞行, 经度: {}, 纬度: {}, 高度: {}, 速度: {}, 半径: {}, 圈数: {}",
					circleParams.JD,
					circleParams.WD,
					circleParams.GD,
					circleParams.SD,
					circleParams.BJ,
					circleParams.QS
				);
				_DJI T_DjiReturnCode return_code = _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS; // 假设成功
				if (return_code != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
				{
					LOG_ERROR("环绕飞行失败, 错误: {}", plane::utils::convertDjiError(return_code));
				}

				return return_code;
			}
		);
	}

	void PSDKAdapter::rotateGimbal(const plane::protocol::GimbalControlPayload& payload)
	{
		LOG_INFO("收到云台控制请求, 俯仰角: {}, 偏航角: {}, 模式: {}", payload.FYJ, payload.PHJ, payload.MS);

		(void)this->executePsdkCommandAsync(
			[payload](void) -> _DJI T_DjiReturnCode
			{
				LOG_INFO("线程池任务: 执行云台控制: 俯仰角: {}, 偏航角: {}, 模式: {}", payload.FYJ, payload.PHJ, payload.MS);
				return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS; // 示例返回值
			}
		);
	}

	void PSDKAdapter::setCameraZoomFactor(const plane::protocol::ZoomControlPayload& payload)
	{
		LOG_INFO(
			"收到相机变焦请求, 相机索引: {}, 相机类型: {}, 变焦倍数: {}",
			payload.XJSY.value_or("null"),
			payload.XJLX.value_or("null"),
			payload.BJB.value_or(-1.0)
		);

		(void)this->executePsdkCommandAsync(
			[payload](void) -> _DJI T_DjiReturnCode
			{
				LOG_INFO(
					"线程池任务: 执行相机变焦: 相机索引: {}, 相机类型: {}, 变焦倍数: {}",
					payload.XJSY.value_or("null"),
					payload.XJLX.value_or("null"),
					payload.BJB.value_or(-1)
				);

				return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS; // 示例返回值
			}
		);
	}

	void PSDKAdapter::setCameraStreamSource(const _DEFINED _VIDEO_SOURCE_TYPE& source)
	{
		LOG_INFO("收到切换视频源请求, 源: {}", source);

		(void)this->executePsdkCommandAsync(
			[source](void) -> _DJI T_DjiReturnCode
			{
				LOG_INFO("线程池任务: 执行切换视频源: 源: {}", source);
				return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS; // 示例返回值
			}
		);
	}

	void PSDKAdapter::sendRawStickData(const plane::protocol::StickDataPayload& payload)
	{
		LOG_INFO("收到虚拟摇杆数据, 油门: {}, 偏航: {}, 俯仰: {}, 横滚: {}", payload.YML, payload.PHL, payload.FYL, payload.HGL);

		(void)this->executePsdkCommandAsync(
			[payload](void) -> _DJI T_DjiReturnCode
			{
				LOG_DEBUG(
					"线程池任务: 发送虚拟摇杆数据: 油门: {}, 偏航: {}, 俯仰: {}, 横滚: {}",
					payload.YML,
					payload.PHL,
					payload.FYL,
					payload.HGL
				);

				return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS; // 示例返回值
			}
		);
	}

	void PSDKAdapter::enableVirtualStick(const plane::protocol::StickModeSwitchPayload& payload)
	{
		LOG_INFO("收到开启虚拟摇杆请求, 模式: {}", payload.YGMS);
		this->virtual_stick_mode_.store(payload.YGMS, _STD memory_order_release); // 域模型/SBZT 由采集循环同步

		(void)this->executePsdkCommandAsync(
			[payload](void) -> _DJI T_DjiReturnCode
			{
				LOG_INFO("线程池任务: 开启虚拟摇杆");
				return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS; // 示例返回值
			}
		);
	}

	void PSDKAdapter::disableVirtualStick(const plane::protocol::StickModeSwitchPayload& payload)
	{
		LOG_INFO("收到关闭虚拟摇杆请求, 模式: {}", payload.YGMS);
		this->virtual_stick_mode_.store(0, _STD memory_order_release);

		(void)this->executePsdkCommandAsync(
			[payload](void) -> _DJI T_DjiReturnCode
			{
				LOG_INFO("线程池任务: 关闭虚拟摇杆");
				return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS; // 示例返回值
			}
		);
	}

	void PSDKAdapter::sendNedVelocityCommand(const plane::protocol::NedVelocityPayload& payload)
	{
		LOG_INFO(
			"收到 NED 速度指令, 北向: {}, 东向: {}, 地向: {}, 偏航角: {}, 模式: {}",
			payload.SDN,
			payload.SDD,
			payload.SDX,
			payload.PHJ,
			payload.MS
		);

		(void)this->executePsdkCommandAsync(
			[payload](void) -> _DJI T_DjiReturnCode
			{
				LOG_DEBUG(
					"线程池任务: 发送 NED 速度指令: 北向: {}, 东向: {}, 地向: {}, 偏航角: {}, 模式: {}",
					payload.SDN,
					payload.SDD,
					payload.SDX,
					payload.PHJ,
					payload.MS
				);
				return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS; // 示例返回值
			}
		);
	}

	_STD future<_DJI T_DjiReturnCode> PSDKAdapter::stopWaypointMissionAsync(void)
	{
		LOG_INFO("收到停止航线任务请求");
		return this->executeWaypointActionAsync(_DJI DJI_WAYPOINT_V3_ACTION_STOP);
	}

	_STD future<_DJI T_DjiReturnCode> PSDKAdapter::pauseWaypointMissionAsync(void)
	{
		LOG_INFO("收到暂停航线任务请求");
		return this->executeWaypointActionAsync(_DJI DJI_WAYPOINT_V3_ACTION_PAUSE);
	}

	_STD future<_DJI T_DjiReturnCode> PSDKAdapter::resumeWaypointMissionAsync(void)
	{
		LOG_INFO("收到恢复航线任务请求");
		return this->executeWaypointActionAsync(_DJI DJI_WAYPOINT_V3_ACTION_RESUME);
	}

	// ... 在这里实现所有其他 PSDK API 的封装 ...

	void PSDKAdapter::commandProcessingLoop(void)
	{
		LOG_INFO("PSDK 命令处理线程已进入循环");
		auto& event_manager { plane::manager::EventManager::getInstance() };
		while (this->run_command_processing_)
		{
			auto& queue { event_manager.getCommandQueue() };
			queue.wait();
			if (!this->run_command_processing_)
			{
				break;
			}
			LOG_INFO("命令处理线程: 被唤醒，队列非空，准备处理事件");
			queue.process();
		}
		LOG_INFO("PSDK 命令处理线程已退出循环");
	}
} // namespace plane::manager

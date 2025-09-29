// raspberry_pi/services/DroneControl/PSDKAdapter/PSDKAdapter.cpp

#include "services/DroneControl/PSDKAdapter/PSDKAdapter.h"

#include <string_view>
#include <cmath>
#include <filesystem>

#include <dji_aircraft_info.h>
#include <dji_camera_manager.h>
#include <dji_error.h>
#include <dji_flight_controller.h>
#include <dji_gimbal.h>
#include <dji_logger.h>

#include "camera_emu/test_payload_cam_emu_base.h"
#include "camera_emu/test_payload_cam_emu_media.h"
#include "camera_manager/test_camera_manager_entry.h"
#include "data_transmission/test_data_transmission.h"
#include "fc_subscription/test_fc_subscription.h"
#include "flight_control/test_flight_control.h"
#include "flight_controller/test_flight_controller_entry.h"
#include "gimbal/test_gimbal_entry.hpp"
#include "gimbal_emu/test_payload_gimbal_emu.h"
#include "hms_manager/hms_manager_entry.h"
#include "liveview/test_liveview_entry.hpp"
#include "perception/test_lidar_entry.hpp"
#include "perception/test_perception_entry.hpp"
#include "perception/test_radar_entry.hpp"
#include "positioning/test_positioning.h"
#include "power_management/test_power_management.h"
#include "waypoint_v3/test_waypoint_v3.h"
#include "widget/test_widget.h"
#include "widget/test_widget_speaker.h"

#include "PSDKAdapter.h"
#include "utils/DjiErrorUtils.h"
#include "utils/EnvironmentCheck.h"
#include "utils/Logger.h"

namespace plane::services
{
	using namespace _STD literals;

	namespace
	{
		_STD string_view aircraftTypeToString(_DJI E_DjiAircraftType type)
		{
			switch (type)
			{
				// M200 V2 Series
				case _DJI DJI_AIRCRAFT_TYPE_M200_V2:
				{
					return "Matrice 200 V2"sv;
				}
				case _DJI DJI_AIRCRAFT_TYPE_M210_V2:
				{
					return "Matrice 210 V2"sv;
				}
				case _DJI DJI_AIRCRAFT_TYPE_M210RTK_V2:
				{
					return "Matrice 210 RTK V2"sv;
				}

				// M300 / M350 Series
				case _DJI DJI_AIRCRAFT_TYPE_M300_RTK:
				{
					return "Matrice 300 RTK"sv;
				}
				case _DJI DJI_AIRCRAFT_TYPE_M350_RTK:
				{
					return "Matrice 350 RTK"sv;
				}

				// M30 Series
				case _DJI DJI_AIRCRAFT_TYPE_M30:
				{
					return "Matrice 30"sv;
				}
				case _DJI DJI_AIRCRAFT_TYPE_M30T:
				{
					return "Matrice 30T"sv;
				}

				// Mavic 3 Enterprise Series
				case _DJI DJI_AIRCRAFT_TYPE_M3E:
				{
					return "Mavic 3 Enterprise"sv;
				}
				case _DJI DJI_AIRCRAFT_TYPE_M3T:
				{
					return "Mavic 3 Thermal"sv;
				}
				case _DJI DJI_AIRCRAFT_TYPE_M3TA:
				{
					return "Mavic 3 TA"sv;
				}

				// Matrice 3D / 3TD Series
				case _DJI DJI_AIRCRAFT_TYPE_M3D:
				{
					return "Matrice 3D"sv;
				}
				case _DJI DJI_AIRCRAFT_TYPE_M3TD:
				{
					return "Matrice 3TD"sv;
				}

				// Matrice 4 Series
				case _DJI DJI_AIRCRAFT_TYPE_M4T:
				{
					return "Matrice 4T"sv;
				}
				case _DJI DJI_AIRCRAFT_TYPE_M4E:
				{
					return "Matrice 4E"sv;
				}
				case _DJI DJI_AIRCRAFT_TYPE_M4TD:
				{
					return "Matrice 4TD"sv;
				}
				case _DJI DJI_AIRCRAFT_TYPE_M4D:
				{
					return "Matrice 4D"sv;
				}

				// Matrice 400 Series
				case _DJI DJI_AIRCRAFT_TYPE_M400:
				{
					return "Matrice 400"sv;
				}

				// Other
				case _DJI DJI_AIRCRAFT_TYPE_FC30:
				{
					return "FlyCart 30"sv;
				}

				case _DJI DJI_AIRCRAFT_TYPE_UNKNOWN:
				default:
				{
					return "Unknown Aircraft"sv;
				}
			}
		}

		inline _STD string_view djiMissionStateToString(_DJI E_DjiWaypointV3MissionState state)
		{
			switch (state)
			{
				case _DJI DJI_WAYPOINT_V3_MISSION_STATE_IDLE:
				{
					return "空闲"sv;
				}
				case _DJI DJI_WAYPOINT_V3_MISSION_STATE_PREPARE:
				{
					return "准备中"sv;
				}
				case _DJI DJI_WAYPOINT_V3_MISSION_STATE_TRANS_MISSION:
				{
					return "传输中"sv;
				}
				case _DJI DJI_WAYPOINT_V3_MISSION_STATE_MISSION:
				{
					return "任务执行中"sv;
				}
				case _DJI DJI_WAYPOINT_V3_MISSION_STATE_BREAK:
				{
					return "任务中断"sv;
				}
				case _DJI DJI_WAYPOINT_V3_MISSION_STATE_RESUME:
				{
					return "任务恢复中"sv;
				}
				case _DJI DJI_WAYPOINT_V3_MISSION_STATE_RETURN_FIRSTPOINT:
				{
					return "返回航线起点"sv;
				}
				default:
				{
					return "未知状态"sv;
				}
			}
		}

		inline _STD string_view djiActionStateToString(_DJI E_DjiWaypointV3ActionState state)
		{
			switch (state)
			{
				case _DJI DJI_WAYPOINT_V3_ACTION_STATE_IDLE:
				{
					return "空闲"sv;
				}
				case _DJI DJI_WAYPOINT_V3_ACTION_STATE_RUNNING:
				{
					return "正在执行"sv;
				}
				case _DJI DJI_WAYPOINT_V3_ACTION_STATE_FINISHED:
				{
					return "执行完成"sv;
				}
				default:
				{
					return "未知状态"sv;
				}
			}
		}
	} // namespace

	PSDKAdapter& PSDKAdapter::getInstance(void) noexcept
	{
		static PSDKAdapter instance {};
		return instance;
	}

	PSDKAdapter::PSDKAdapter(void) noexcept:
		m_commandPool(_STD make_unique<ThreadPool>(2)),
		m_lastUpdateTime(_STD_CHRONO steady_clock::time_point::min())
	{}

	PSDKAdapter::~PSDKAdapter(void) noexcept
	{
		stop();
	}

	bool PSDKAdapter::start(void) noexcept
	{
		m_isStopping = false;
		if (m_runAcquisition.exchange(true))
		{
			LOG_WARN("PSDKAdapter 数据采集线程已经启动，请勿重复调用 start()。");
			return false;
		}
		LOG_INFO("正在启动 PSDK 数据采集线程...");
		m_acquisitionThread = _STD thread(&PSDKAdapter::acquisitionLoop, this);
		return true;
	}

	void PSDKAdapter::stop(_STD chrono::milliseconds timeout) noexcept
	{
		m_isStopping = true;
		LOG_INFO("PSDKAdapter 开始停止流程，超时时间: {}ms...", timeout.count());

		if (m_runAcquisition.exchange(false))
		{
			if (m_acquisitionThread.joinable())
			{
				m_acquisitionThread.join();
				LOG_INFO("PSDK 数据采集线程已停止。");
			}
		}

		if (m_commandPool)
		{
			auto future = _STD async(_STD launch::async,
									 [this]()
									 {
										 m_commandPool.reset();
									 });

			LOG_INFO("正在等待命令线程池中的任务完成...");
			if (future.wait_for(timeout) == _STD future_status::timeout)
			{
				LOG_ERROR("关闭 PSDK 命令线程池超时。可能有一个任务仍在后台运行。stop() 函数将不再等待，继续关闭流程。");
			}
			else
			{
				LOG_INFO("PSDK 命令执行线程池已成功关闭。");
			}
		}
	}

	bool PSDKAdapter::setup(void) noexcept
	{
		LOG_INFO("正在订阅遥测数据主题...");

		auto subscribe = [&](_DJI E_DjiFcSubscriptionTopic topic, _STD string_view topicName)
		{
			if (_DJI T_DjiReturnCode returnCode {
					_DJI DjiFcSubscription_SubscribeTopic(topic, _DJI DJI_DATA_SUBSCRIPTION_TOPIC_10_HZ, nullptr) };
				returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
			{
				LOG_WARN("订阅主题 '{}' 失败 (飞机不支持?), 错误: {}, 错误码: {:#08x}",
						 topicName,
						 plane::utils::djiReturnCodeToString(returnCode),
						 returnCode);
				return false;
			}
			return true;
		};

		m_sub_status.positionFused		 = subscribe(_DJI DJI_FC_SUBSCRIPTION_TOPIC_POSITION_FUSED, "POSITION_FUSED"sv);
		m_sub_status.altitudeFused		 = subscribe(_DJI DJI_FC_SUBSCRIPTION_TOPIC_ALTITUDE_FUSED, "ALTITUDE_FUSED"sv);
		m_sub_status.altitudeOfHomepoint = subscribe(_DJI DJI_FC_SUBSCRIPTION_TOPIC_ALTITUDE_OF_HOMEPOINT, "ALTITUDE_OF_HOMEPOINT"sv);
		m_sub_status.quaternion			 = subscribe(_DJI DJI_FC_SUBSCRIPTION_TOPIC_QUATERNION, "QUATERNION"sv);
		m_sub_status.velocity			 = subscribe(_DJI DJI_FC_SUBSCRIPTION_TOPIC_VELOCITY, "VELOCITY"sv);
		m_sub_status.batteryInfo		 = subscribe(_DJI DJI_FC_SUBSCRIPTION_TOPIC_BATTERY_INFO, "BATTERY_INFO"sv);
		m_sub_status.gimbalAngles		 = subscribe(_DJI DJI_FC_SUBSCRIPTION_TOPIC_GIMBAL_ANGLES, "GIMBAL_ANGLES"sv);

		if (_DJI T_DjiReturnCode returnCode { _DJI DjiWaypointV3_RegMissionStateCallback(missionStateCallbackEntry) };
			returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
		{
			LOG_ERROR("注册 Waypoint V3 任务状态回调失败, 错误: {}", plane::utils::djiReturnCodeToString(returnCode));
		}

		if (_DJI T_DjiReturnCode returnCode { DjiWaypointV3_RegActionStateCallback(actionStateCallbackEntry) };
			returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
		{
			LOG_ERROR("注册 Waypoint V3 动作状态回调失败, 错误: {}", plane::utils::djiReturnCodeToString(returnCode));
		}

		LOG_INFO("PSDK 适配器准备就绪。");
		return true;
	}

	void PSDKAdapter::cleanup(void) noexcept
	{
		LOG_INFO("正在清理 PSDK 适配器 (取消已订阅的主题)...");

		auto unsubscribe = [&](bool was_subscribed, _DJI E_DjiFcSubscriptionTopic topic, _STD string_view topicName)
		{
			if (was_subscribed)
			{
				if (_DJI T_DjiReturnCode returnCode { _DJI DjiFcSubscription_UnSubscribeTopic(topic) };
					returnCode != _DJI	 DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
				{
					LOG_WARN("取消订阅主题 '{}' 失败, 错误: {}, 错误码: {:#08x}",
							 topicName,
							 plane::utils::djiReturnCodeToString(returnCode),
							 returnCode);
				}
				else
				{
					LOG_DEBUG("成功取消订阅主题 '{}'。", topicName);
				}
			}
		};

		unsubscribe(m_sub_status.positionFused, _DJI DJI_FC_SUBSCRIPTION_TOPIC_POSITION_FUSED, "POSITION_FUSED"sv);
		unsubscribe(m_sub_status.altitudeFused, _DJI DJI_FC_SUBSCRIPTION_TOPIC_ALTITUDE_FUSED, "ALTITUDE_FUSED"sv);
		unsubscribe(m_sub_status.altitudeOfHomepoint, _DJI DJI_FC_SUBSCRIPTION_TOPIC_ALTITUDE_OF_HOMEPOINT, "ALTITUDE_OF_HOMEPOINT"sv);
		unsubscribe(m_sub_status.quaternion, _DJI DJI_FC_SUBSCRIPTION_TOPIC_QUATERNION, "QUATERNION"sv);
		unsubscribe(m_sub_status.velocity, _DJI DJI_FC_SUBSCRIPTION_TOPIC_VELOCITY, "VELOCITY"sv);
		unsubscribe(m_sub_status.batteryInfo, _DJI DJI_FC_SUBSCRIPTION_TOPIC_BATTERY_INFO, "BATTERY_INFO"sv);
		unsubscribe(m_sub_status.gimbalAngles, _DJI DJI_FC_SUBSCRIPTION_TOPIC_GIMBAL_ANGLES, "GIMBAL_ANGLES"sv);
	}

	void PSDKAdapter::acquisitionLoop(void) noexcept
	{
		while (m_runAcquisition)
		{
			auto						   startTime { _STD_CHRONO steady_clock::now() };
			plane::protocol::StatusPayload current_payload {};
			_DJI T_DjiDataTimestamp		   timestamp {};

			if (_DJI T_DjiFcSubscriptionPositionFused pos {};
				m_sub_status.positionFused &&
				(_DJI DjiFcSubscription_GetLatestValueOfTopic(_DJI DJI_FC_SUBSCRIPTION_TOPIC_POSITION_FUSED,
															  (uint8_t*)&pos,
															  sizeof(pos),
															  &timestamp) == _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS))
			{
				current_payload.SXZT.GPSSXSL = pos.visibleSatelliteNumber;
				current_payload.DQJD		 = pos.longitude * RAD_TO_DEG; // 当前经度 (度)
				current_payload.DQWD		 = pos.latitude * RAD_TO_DEG;  // 当前纬度 (度)
				current_payload.JDGD		 = pos.altitude;			   // 海拔高度
				// TODO: 根据 pos.gnssFixStatus 和 pos.gpsFixStatus 来填充 SFSL 和 SXDW
			}

			if (_DJI T_DjiFcSubscriptionAltitudeFused fused_alt {};
				m_sub_status.altitudeFused &&
				(_DJI DjiFcSubscription_GetLatestValueOfTopic(_DJI DJI_FC_SUBSCRIPTION_TOPIC_ALTITUDE_FUSED,
															  (uint8_t*)&fused_alt,
															  sizeof(fused_alt),
															  &timestamp) == _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS))
			{
				if (_DJI T_DjiFcSubscriptionAltitudeFused hp_alt {};
					m_sub_status.altitudeOfHomepoint &&
					(_DJI DjiFcSubscription_GetLatestValueOfTopic(_DJI DJI_FC_SUBSCRIPTION_TOPIC_ALTITUDE_OF_HOMEPOINT,
																  (uint8_t*)&hp_alt,
																  sizeof(hp_alt),
																  &timestamp) == _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS))
				{
					current_payload.XDQFGD = fused_alt - hp_alt; // 相对起飞点高度
				}
			}

			if (_DJI T_DjiFcSubscriptionQuaternion q {};
				m_sub_status.quaternion &&
				(_DJI DjiFcSubscription_GetLatestValueOfTopic(_DJI DJI_FC_SUBSCRIPTION_TOPIC_QUATERNION, (uint8_t*)&q, sizeof(q), &timestamp) ==
				 _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS))
			{
				quaternionToEulerAngle(q, current_payload.FJHGJ, current_payload.FJFYJ, current_payload.FJPHJ);
			}

			if (_DJI T_DjiFcSubscriptionVelocity vel {};
				m_sub_status.velocity && (_DJI DjiFcSubscription_GetLatestValueOfTopic(_DJI DJI_FC_SUBSCRIPTION_TOPIC_VELOCITY,
																					   (uint8_t*)&vel,
																					   sizeof(vel),
																					   &timestamp) == _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS))
			{
				current_payload.VY	 = vel.data.x;	// 北向速度 (North)
				current_payload.VX	 = vel.data.y;	// 东向速度 (East)
				current_payload.VZ	 = -vel.data.z; // 地向速度 (Down). PSDK z 轴向上为正, 我们的协议下为正, 所以取反.
				current_payload.SPSD = _STD sqrt(vel.data.x * vel.data.x + vel.data.y * vel.data.y); // 水平速度
				current_payload.CZSD = vel.data.z;													 // 垂直速度
			}

			if (_DJI T_DjiFcSubscriptionSingleBatteryInfo batt {};
				m_sub_status.batteryInfo &&
				(_DJI DjiFcSubscription_GetLatestValueOfTopic(_DJI DJI_FC_SUBSCRIPTION_TOPIC_BATTERY_INFO,
															  (uint8_t*)&batt,
															  sizeof(batt),
															  &timestamp) == _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS))
			{
				current_payload.DCXX.SYDL = batt.batteryCapacityPercent;
				current_payload.DCXX.ZDY  = batt.currentVoltage;
			}

			if (_DJI T_DjiFcSubscriptionGimbalAngles gimbalAngle {};
				m_sub_status.gimbalAngles &&
				(_DJI DjiFcSubscription_GetLatestValueOfTopic(_DJI DJI_FC_SUBSCRIPTION_TOPIC_GIMBAL_ANGLES,
															  (uint8_t*)&gimbalAngle,
															  sizeof(gimbalAngle),
															  &timestamp) == _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS))
			{
				current_payload.YTFY = gimbalAngle.x;
				current_payload.YTHG = gimbalAngle.y;
				current_payload.YTPH = gimbalAngle.z;
			}

			if (_DJI T_DjiAircraftInfoBaseInfo aircraftInfo {};
				_DJI DjiAircraftInfo_GetBaseInfo(&aircraftInfo) == _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
			{
				current_payload.XH = aircraftTypeToString(aircraftInfo.aircraftType);
			}
			else
			{
				current_payload.XH = "N/A";
			}

			current_payload.CJ = "DJI";

			{
				_STD lock_guard<_STD mutex> lock(m_payloadMutex);
				m_latestPayload = current_payload;
			}

			{
				_STD lock_guard<_STD mutex>	   lock(m_healthMutex);
				m_lastUpdateTime = _STD_CHRONO steady_clock::now();
			}

			auto endTime { _STD_CHRONO steady_clock::now() };
			if (auto elapsedTime { _STD_CHRONO duration_cast<_STD_CHRONO milliseconds>(endTime - startTime) };
				elapsedTime < ACQUISITION_INTERVAL)
			{
				_STD this_thread::sleep_for(ACQUISITION_INTERVAL - elapsedTime);
			}
		}
	}

	void PSDKAdapter::missionStateCallback(_DJI T_DjiWaypointV3MissionState missionState)
	{
		LOG_INFO("[航线任务状态] 状态: {}, 当前航点: {}, 航线ID: {}",
				 djiMissionStateToString(missionState.state),
				 missionState.currentWaypointIndex,
				 missionState.wayLineId);
	}

	void PSDKAdapter::actionStateCallback(_DJI T_DjiWaypointV3ActionState actionState)
	{
		LOG_INFO("[航线动作状态] 状态: {}, 航点: {}, 动作组: {}, 动作ID: {}",
				 djiActionStateToString(actionState.state),
				 actionState.currentWaypointIndex,
				 actionState.actionGroupId,
				 actionState.actionId);
	}

	_DJI T_DjiReturnCode PSDKAdapter::missionStateCallbackEntry(_DJI T_DjiWaypointV3MissionState missionState)
	{
		PSDKAdapter::getInstance().missionStateCallback(missionState);
		return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
	}

	_DJI T_DjiReturnCode PSDKAdapter::actionStateCallbackEntry(_DJI T_DjiWaypointV3ActionState actionState)
	{
		PSDKAdapter::getInstance().actionStateCallback(actionState);
		return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
	}

	_STD future<_DJI T_DjiReturnCode> PSDKAdapter::stopWaypointMission(void)
	{
		return m_commandPool->enqueue(
			[this]() -> _DJI T_DjiReturnCode
			{
				_STD lock_guard<_STD mutex> lock(m_psdkCommandMutex);
				LOG_INFO("线程池任务：发送停止航线指令...");
				return _DJI DjiWaypointV3_Action(_DJI DJI_WAYPOINT_V3_ACTION_STOP);
			});
	}

	_STD future<T_DjiReturnCode> PSDKAdapter::pauseWaypointMission(void)
	{
		return m_commandPool->enqueue(
			[this]() -> _DJI T_DjiReturnCode
			{
				try
				{
					_STD lock_guard<_STD mutex> lock(m_psdkCommandMutex);
					LOG_INFO("线程池任务：发送暂停航线指令...");
					_DJI T_DjiReturnCode returnCode { _DJI DjiWaypointV3_Action(_DJI DJI_WAYPOINT_V3_ACTION_PAUSE) };
					if (returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
					{
						LOG_ERROR("发送暂停航线指令失败, 错误: {} (0x{:08X})", plane::utils::djiReturnCodeToString(returnCode), returnCode);
					}
					return returnCode;
				}
				catch (const _STD exception& e)
				{
					LOG_ERROR("PSDKAdapter::pauseWaypointMission 任务捕获到标准异常: {}", e.what());
					return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_UNKNOWN;
				}
				catch (...)
				{
					LOG_ERROR("PSDKAdapter::pauseWaypointMission 任务捕获到未知异常！");
					return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_UNKNOWN;
				}
			});
	}

	_STD future<T_DjiReturnCode> PSDKAdapter::resumeWaypointMission(void)
	{
		return m_commandPool->enqueue(
			[this]() -> _DJI T_DjiReturnCode
			{
				try
				{
					_STD lock_guard<_STD mutex> lock(m_psdkCommandMutex);
					LOG_INFO("线程池任务：发送恢复航线指令...");
					_DJI T_DjiReturnCode returnCode { _DJI DjiWaypointV3_Action(_DJI DJI_WAYPOINT_V3_ACTION_RESUME) };
					if (returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
					{
						LOG_ERROR("发送恢复航线指令失败, 错误: {} (0x{:08X})", plane::utils::djiReturnCodeToString(returnCode), returnCode);
					}
					return returnCode;
				}
				catch (const _STD exception& e)
				{
					LOG_ERROR("PSDKAdapter::resumeWaypointMission 任务捕获到标准异常: {}", e.what());
					return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_UNKNOWN;
				}
				catch (...)
				{
					LOG_ERROR("PSDKAdapter::resumeWaypointMission 任务捕获到未知异常！");
					return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_UNKNOWN;
				}
			});
	}

	_STD_CHRONO steady_clock::time_point PSDKAdapter::getLastUpdateTime(void) const noexcept
	{
		_STD lock_guard<_STD mutex> lock(m_healthMutex);
		return m_lastUpdateTime;
	}

	plane::protocol::StatusPayload PSDKAdapter::getLatestStatusPayload(void) const noexcept
	{
		_STD lock_guard<_STD mutex> lock(m_payloadMutex);
		return m_latestPayload;
	}

	void PSDKAdapter::quaternionToEulerAngle(const _DJI T_DjiFcSubscriptionQuaternion& q, double& roll, double& pitch, double& yaw) noexcept
	{
		const double sinr_cosp { 2 * (q.q0 * q.q1 + q.q2 * q.q3) };
		const double cosr_cosp { 1 - 2 * (q.q1 * q.q1 + q.q2 * q.q2) };
		roll = _STD	 atan2(sinr_cosp, cosr_cosp) * 180.0 / MATH_PI;

		const double sinp { 2 * (q.q0 * q.q2 - q.q3 * q.q1) };
		if (_STD abs(sinp) >= 1)
		{
			pitch = _STD copysign(MATH_PI / 2, sinp) * 180.0 / MATH_PI;
		}
		else
		{
			pitch = _STD asin(sinp) * 180.0 / MATH_PI;
		}

		const double siny_cosp { 2 * (q.q0 * q.q3 + q.q1 * q.q2) };
		const double cosy_cosp { 1 - 2 * (q.q2 * q.q2 + q.q3 * q.q3) };
		yaw = _STD	 atan2(siny_cosp, cosy_cosp) * 180.0 / MATH_PI;
	}

	_STD future<_DJI T_DjiReturnCode> PSDKAdapter::takeoff(const plane::protocol::TakeoffPayload& takeoffParams)
	{
		return m_commandPool->enqueue(
			[this]() -> _DJI T_DjiReturnCode
			{
				try
				{
					_STD lock_guard<_STD mutex> lock(m_psdkCommandMutex);

					if (!plane::utils::isStandardProceduresEnabled())
					{
						LOG_WARN("环境变量 FULL_PSDK 未设置或不为 '1', 起飞操作被禁止。");
						return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_NONSUPPORT;
					}

					LOG_INFO("线程池任务：执行起飞...");
					_DJI T_DjiReturnCode returnCode { _DJI DjiFlightController_StartTakeoff() };
					if (returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
					{
						LOG_ERROR("PSDK API DjiFlightController_StartTakeoff() 调用失败, 错误: {}, 错误码: 0x{:08X}",
								  plane::utils::djiReturnCodeToString(returnCode),
								  returnCode);
					}
					return returnCode;
				}
				catch (const _STD exception& e)
				{
					LOG_ERROR("PSDKAdapter::takeoff 任务在线程池中捕获到标准异常: {}", e.what());
					return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_UNKNOWN;
				}
				catch (...)
				{
					LOG_ERROR("PSDKAdapter::takeoff 任务在线程池中捕获到未知异常！");
					return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_UNKNOWN;
				}
			});
	}

	_STD future<_DJI T_DjiReturnCode> PSDKAdapter::goHome(void)
	{
		return m_commandPool->enqueue(
			[this]() -> _DJI T_DjiReturnCode
			{
				try
				{
					_STD lock_guard<_STD mutex> lock(m_psdkCommandMutex);
					if (!plane::utils::isStandardProceduresEnabled())
					{
						return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_NONSUPPORT;
					}

					LOG_INFO("线程池任务：执行返航...");
					_DJI T_DjiReturnCode returnCode { _DJI DjiFlightController_StartGoHome() };
					if (returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
					{
						LOG_ERROR("PSDK API DjiFlightController_StartGoHome() 调用失败, 错误: {}, 错误码: 0x{:08X}",
								  plane::utils::djiReturnCodeToString(returnCode),
								  returnCode);
					}
					return returnCode;
				}
				catch (const _STD exception& e)
				{
					LOG_ERROR("PSDKAdapter::goHome 任务在线程池中捕获到标准异常: {}", e.what());
					return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_UNKNOWN;
				}
				catch (...)
				{
					LOG_ERROR("PSDKAdapter::goHome 任务在线程池中捕获到未知异常！");
					return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_UNKNOWN;
				}
			});
	}

	_STD future<_DJI T_DjiReturnCode> PSDKAdapter::hover(void)
	{
		return m_commandPool->enqueue(
			[this]() -> _DJI T_DjiReturnCode
			{
				try
				{
					_STD lock_guard<_STD mutex> lock(m_psdkCommandMutex);
					if (!plane::utils::isStandardProceduresEnabled())
					{
						return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_NONSUPPORT;
					}

					LOG_INFO("线程池任务：执行悬停...");
					_DJI T_DjiReturnCode returnCode { _DJI DjiFlightController_ExecuteEmergencyBrakeAction() };
					if (returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
					{
						LOG_ERROR("PSDK API DjiFlightController_ExecuteEmergencyBrakeAction() 调用失败, 错误: {}, 错误码: 0x{:08X}",
								  plane::utils::djiReturnCodeToString(returnCode),
								  returnCode);
					}
					return returnCode;
				}
				catch (const _STD exception& e)
				{
					LOG_ERROR("PSDKAdapter::hover 任务在线程池中捕获到标准异常: {}", e.what());
					return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_UNKNOWN;
				}
				catch (...)
				{
					LOG_ERROR("PSDKAdapter::hover 任务在线程池中捕获到未知异常！");
					return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_UNKNOWN;
				}
			});
	}

	_STD future<_DJI T_DjiReturnCode> PSDKAdapter::land(void)
	{
		return m_commandPool->enqueue(
			[this]() -> _DJI T_DjiReturnCode
			{
				try
				{
					_STD lock_guard<_STD mutex> lock(m_psdkCommandMutex);
					if (!plane::utils::isStandardProceduresEnabled())
					{
						return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_NONSUPPORT;
					}

					LOG_INFO("线程池任务：执行降落...");
					_DJI T_DjiReturnCode returnCode { _DJI DjiFlightController_StartLanding() };
					if (returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
					{
						LOG_ERROR("PSDK API DjiFlightController_StartLanding() 调用失败, 错误: {}, 错误码: 0x{:08X}",
								  plane::utils::djiReturnCodeToString(returnCode),
								  returnCode);
					}
					return returnCode;
				}
				catch (const _STD exception& e)
				{
					LOG_ERROR("PSDKAdapter::land 任务在线程池中捕获到标准异常: {}", e.what());
					return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_UNKNOWN;
				}
				catch (...)
				{
					LOG_ERROR("PSDKAdapter::land 任务在线程池中捕获到未知异常！");
					return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_UNKNOWN;
				}
			});
	}

	_STD future<_DJI T_DjiReturnCode> PSDKAdapter::waypointV3(_STD string kmzFilePath)
	{
		return m_commandPool->enqueue(
			[this, path = _STD move(kmzFilePath)]() -> _DJI T_DjiReturnCode
			{
				try
				{
					if (m_isStopping)
					{
						LOG_WARN("应用程序正在关闭，航线任务 '{}' 被取消。", path);
						return _DJI DJI_ERROR_WAYPOINT_V3_MODULE_CODE_USER_EXIT;
					}

					_STD lock_guard<_STD mutex> lock(m_psdkCommandMutex);
					if (m_isStopping)
					{
						LOG_WARN("应用程序正在关闭，航线任务 '{}' 被取消。", path);
						return _DJI DJI_ERROR_WAYPOINT_V3_MODULE_CODE_USER_EXIT;
					}

					if (!plane::utils::isStandardProceduresEnabled())
					{
						LOG_WARN("环境变量 FULL_PSDK 未设置或不为 '1', 航点任务操作被禁止。");
						return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_NONSUPPORT;
					}

					if (!_STD_FS exists(path))
					{
						LOG_ERROR("航线任务失败：KMZ 文件不存在: {}", path);
						return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
					}

					LOG_INFO("线程池任务：开始执行航线任务, 文件: {}", path);
					T_DjiReturnCode returnCode { _DJI DjiTest_WaypointV3RunSampleWithKmzFilePath(path.c_str()) };
					if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
					{
						LOG_ERROR("航线任务 '{}' 执行失败或被中断，最终返回错误: {} (0x{:08X})",
								  path,
								  plane::utils::djiReturnCodeToString(returnCode),
								  returnCode);
					}
					else
					{
						LOG_INFO("航线任务 '{}' 成功完成。", path);
					}
					return returnCode;
				}
				catch (const _STD exception& e)
				{
					LOG_ERROR("PSDKAdapter::waypointV3 任务在线程池中捕获到标准异常: {}", e.what());
					return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_UNKNOWN;
				}
				catch (...)
				{
					LOG_ERROR("PSDKAdapter::waypointV3 任务在线程池中捕获到未知异常！");
					return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_UNKNOWN;
				}
			});
	}

	_STD future<_DJI T_DjiReturnCode> PSDKAdapter::setControlStrategy(int strategyCode)
	{
		return m_commandPool->enqueue(
			[this, strategyCode]() -> _DJI T_DjiReturnCode
			{
				try
				{
					_STD lock_guard<_STD mutex> lock(m_psdkCommandMutex);
					if (!plane::utils::isStandardProceduresEnabled())
					{
						return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_NONSUPPORT;
					}

					LOG_INFO("线程池任务：设置云台控制策略, 策略代码: {}", strategyCode);
					return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
				}
				catch (const _STD exception& e)
				{
					LOG_ERROR("PSDKAdapter::setControlStrategy 任务在线程池中捕获到标准异常: {}", e.what());
					return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_UNKNOWN;
				}
				catch (...)
				{
					LOG_ERROR("PSDKAdapter::setControlStrategy 任务在线程池中捕获到未知异常！");
					return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_UNKNOWN;
				}
			});
	}

	_STD future<_DJI T_DjiReturnCode> PSDKAdapter::flyCircleAroundPoint(const plane::protocol::CircleFlyPayload& circleParams)
	{
		return m_commandPool->enqueue(
			[this, params = circleParams]() -> _DJI T_DjiReturnCode
			{
				try
				{
					_STD lock_guard<_STD mutex> lock(m_psdkCommandMutex);
					if (!plane::utils::isStandardProceduresEnabled())
					{
						return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_NONSUPPORT;
					}

					LOG_INFO("线程池任务：调用智能环绕, 参数: lat={}, lon={}, alt={}, r={}, spd={}",
							 params.WD,
							 params.JD,
							 params.GD,
							 params.BJ,
							 params.SD);
					return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
				}
				catch (const _STD exception& e)
				{
					LOG_ERROR("PSDKAdapter::flyCircleAroundPoint 任务在线程池中捕获到标准异常: {}", e.what());
					return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_UNKNOWN;
				}
				catch (...)
				{
					LOG_ERROR("PSDKAdapter::flyCircleAroundPoint 任务在线程池中捕获到未知异常！");
					return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_UNKNOWN;
				}
			});
	}

	// ... 在这里实现所有其他 PSDK API 的封装 ...
} // namespace plane::services

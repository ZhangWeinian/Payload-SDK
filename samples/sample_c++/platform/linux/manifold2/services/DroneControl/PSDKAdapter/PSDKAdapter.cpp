// manifold2/services/DroneControl/PSDKAdapter/PSDKAdapter.cpp

#include "services/DroneControl/PSDKAdapter/PSDKAdapter.h"

#include <cmath>

#include <dji_aircraft_info.h>
#include <dji_camera_manager.h>
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

#include "utils/EnvironmentCheck.h"
#include "utils/Logger.h"

namespace plane::services
{
	namespace
	{
		const char* aircraftTypeToString(_DJI E_DjiAircraftType type)
		{
			switch (type)
			{
				// M200 V2 Series
				case _DJI DJI_AIRCRAFT_TYPE_M200_V2:
				{
					return "Matrice 200 V2";
				}
				case _DJI DJI_AIRCRAFT_TYPE_M210_V2:
				{
					return "Matrice 210 V2";
				}
				case _DJI DJI_AIRCRAFT_TYPE_M210RTK_V2:
				{
					return "Matrice 210 RTK V2";
				}

				// M300 / M350 Series
				case _DJI DJI_AIRCRAFT_TYPE_M300_RTK:
				{
					return "Matrice 300 RTK";
				}
				case _DJI DJI_AIRCRAFT_TYPE_M350_RTK:
				{
					return "Matrice 350 RTK";
				}

				// M30 Series
				case _DJI DJI_AIRCRAFT_TYPE_M30:
				{
					return "Matrice 30";
				}
				case _DJI DJI_AIRCRAFT_TYPE_M30T:
				{
					return "Matrice 30T";
				}

				// Mavic 3 Enterprise Series
				case _DJI DJI_AIRCRAFT_TYPE_M3E:
				{
					return "Mavic 3 Enterprise";
				}
				case _DJI DJI_AIRCRAFT_TYPE_M3T:
				{
					return "Mavic 3 Thermal";
				}
				case _DJI DJI_AIRCRAFT_TYPE_M3TA:
				{
					return "Mavic 3 TA";
				}

				// Matrice 3D / 3TD Series
				case _DJI DJI_AIRCRAFT_TYPE_M3D:
				{
					return "Matrice 3D";
				}
				case _DJI DJI_AIRCRAFT_TYPE_M3TD:
				{
					return "Matrice 3TD";
				}

				// Matrice 4 Series
				case _DJI DJI_AIRCRAFT_TYPE_M4T:
				{
					return "Matrice 4T";
				}
				case _DJI DJI_AIRCRAFT_TYPE_M4E:
				{
					return "Matrice 4E";
				}
				case _DJI DJI_AIRCRAFT_TYPE_M4TD:
				{
					return "Matrice 4TD";
				}
				case _DJI DJI_AIRCRAFT_TYPE_M4D:
				{
					return "Matrice 4D";
				}

				// Matrice 400 Series
				case _DJI DJI_AIRCRAFT_TYPE_M400:
				{
					return "Matrice 400";
				}

				// Other
				case _DJI DJI_AIRCRAFT_TYPE_FC30:
				{
					return "FlyCart 30";
				}

				case _DJI DJI_AIRCRAFT_TYPE_UNKNOWN:
				default:
				{
					return "Unknown Aircraft";
				}
			}
		}
	} // namespace

	PSDKAdapter& PSDKAdapter::getInstance(void) noexcept
	{
		static PSDKAdapter instance {};
		return instance;
	}

	bool PSDKAdapter::setup(void) noexcept
	{
		_DJI T_DjiReturnCode returnCode {};
		LOG_INFO("正在订阅遥测数据主题...");

		returnCode = _DJI DjiFcSubscription_SubscribeTopic(_DJI DJI_FC_SUBSCRIPTION_TOPIC_POSITION_FUSED,
														   _DJI DJI_DATA_SUBSCRIPTION_TOPIC_10_HZ,
														   nullptr);
		if (returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
		{
			LOG_ERROR("订阅 TOPIC_POSITION_FUSED 失败, 错误码: {:#08x}", returnCode);
		}
		returnCode = _DJI DjiFcSubscription_SubscribeTopic(_DJI DJI_FC_SUBSCRIPTION_TOPIC_ALTITUDE_FUSED,
														   _DJI DJI_DATA_SUBSCRIPTION_TOPIC_10_HZ,
														   nullptr);
		if (returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
		{
			LOG_ERROR("订阅 TOPIC_ALTITUDE_FUSED 失败, 错误码: {:#08x}", returnCode);
		}

		returnCode =
			_DJI DjiFcSubscription_SubscribeTopic(_DJI DJI_FC_SUBSCRIPTION_TOPIC_QUATERNION, _DJI DJI_DATA_SUBSCRIPTION_TOPIC_10_HZ, nullptr);
		if (returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
		{
			LOG_ERROR("订阅 TOPIC_QUATERNION 失败, 错误码: {:#08x}", returnCode);
		}

		returnCode =
			_DJI DjiFcSubscription_SubscribeTopic(_DJI DJI_FC_SUBSCRIPTION_TOPIC_VELOCITY, _DJI DJI_DATA_SUBSCRIPTION_TOPIC_10_HZ, nullptr);
		if (returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
		{
			LOG_ERROR("订阅 TOPIC_VELOCITY 失败, 错误码: {:#08x}", returnCode);
		}

		returnCode =
			_DJI DjiFcSubscription_SubscribeTopic(_DJI DJI_FC_SUBSCRIPTION_TOPIC_BATTERY_INFO, _DJI DJI_DATA_SUBSCRIPTION_TOPIC_1_HZ, nullptr);
		if (returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
		{
			LOG_ERROR("订阅 TOPIC_BATTERY_INFO 失败, 错误码: {:#08x}", returnCode);
		}

		returnCode =
			_DJI DjiFcSubscription_SubscribeTopic(_DJI DJI_FC_SUBSCRIPTION_TOPIC_GIMBAL_ANGLES, _DJI DJI_DATA_SUBSCRIPTION_TOPIC_10_HZ, nullptr);
		if (returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
		{
			LOG_ERROR("订阅 TOPIC_GIMBAL_ANGLES 失败, 错误码: {:#08x}", returnCode);
		}

		LOG_INFO("PSDK 适配器准备就绪。");
		return true;
	}

	void PSDKAdapter::cleanup(void) noexcept
	{
		LOG_INFO("正在清理 PSDK 适配器...");
		_DJI DjiFcSubscription_UnSubscribeTopic(_DJI DJI_FC_SUBSCRIPTION_TOPIC_POSITION_FUSED);
		_DJI DjiFcSubscription_UnSubscribeTopic(_DJI DJI_FC_SUBSCRIPTION_TOPIC_QUATERNION);
		_DJI DjiFcSubscription_UnSubscribeTopic(_DJI DJI_FC_SUBSCRIPTION_TOPIC_VELOCITY);
		_DJI DjiFcSubscription_UnSubscribeTopic(_DJI DJI_FC_SUBSCRIPTION_TOPIC_ALTITUDE_FUSED);
		_DJI DjiFcSubscription_UnSubscribeTopic(_DJI DJI_FC_SUBSCRIPTION_TOPIC_BATTERY_INFO);
		_DJI DjiFcSubscription_UnSubscribeTopic(_DJI DJI_FC_SUBSCRIPTION_TOPIC_GIMBAL_ANGLES);
	}

	plane::protocol::StatusPayload PSDKAdapter::getLatestStatusPayload(void) noexcept
	{
		plane::protocol::StatusPayload payload {};
		_DJI T_DjiDataTimestamp		   timestamp {};

		if (_DJI T_DjiFcSubscriptionPositionFused pos {};
			_DJI DjiFcSubscription_GetLatestValueOfTopic(_DJI DJI_FC_SUBSCRIPTION_TOPIC_POSITION_FUSED,
														 (uint8_t*)&pos,
														 sizeof(pos),
														 &timestamp) == _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
		{
			payload.SXZT.GPSSXSL = pos.visibleSatelliteNumber;
			payload.DQJD		 = pos.longitude; // 当前经度
			payload.DQWD		 = pos.latitude;  // 当前纬度
			payload.JDGD		 = pos.altitude;  // 海拔高度
												  // TODO: 根据 pos.gnssFixStatus 和 pos.gpsFixStatus 来填充 SFSL 和 SXDW
		}

		if (_DJI T_DjiFcSubscriptionAltitudeFused fused_alt {};
			_DJI DjiFcSubscription_GetLatestValueOfTopic(_DJI DJI_FC_SUBSCRIPTION_TOPIC_ALTITUDE_FUSED,
														 (uint8_t*)&fused_alt,
														 sizeof(fused_alt),
														 &timestamp) == _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
		{
			payload.XDQFGD = fused_alt; // 相对起飞点高度
		}

		if (_DJI T_DjiFcSubscriptionQuaternion q {};
			_DJI DjiFcSubscription_GetLatestValueOfTopic(_DJI DJI_FC_SUBSCRIPTION_TOPIC_QUATERNION, (uint8_t*)&q, sizeof(q), &timestamp) ==
			_DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
		{
			quaternionToEulerAngle(q, payload.FJHGJ, payload.FJFYJ, payload.FJPHJ);
		}

		if (_DJI T_DjiFcSubscriptionVelocity vel {};
			DjiFcSubscription_GetLatestValueOfTopic(_DJI DJI_FC_SUBSCRIPTION_TOPIC_VELOCITY, (uint8_t*)&vel, sizeof(vel), &timestamp) ==
			_DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
		{
			payload.VY	 = vel.data.x;	// 北向速度 (North)
			payload.VX	 = vel.data.y;	// 东向速度 (East)
			payload.VZ	 = -vel.data.z; // 地向速度 (Down). PSDK z 轴向上为正, 我们的协议下为正, 所以取反.
			payload.SPSD = _STD sqrt(vel.data.x * vel.data.x + vel.data.y * vel.data.y); // 水平速度
			payload.CZSD = vel.data.z;													 // 垂直速度
		}

		if (_DJI T_DjiFcSubscriptionSingleBatteryInfo batt {};
			_DJI DjiFcSubscription_GetLatestValueOfTopic(_DJI DJI_FC_SUBSCRIPTION_TOPIC_BATTERY_INFO,
														 (uint8_t*)&batt,
														 sizeof(batt),
														 &timestamp) == _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
		{
			payload.DCXX.SYDL = batt.batteryCapacityPercent;
			payload.DCXX.ZDY  = batt.currentVoltage;
		}

		if (_DJI T_DjiFcSubscriptionGimbalAngles gimbalAngle {};
			_DJI DjiFcSubscription_GetLatestValueOfTopic(_DJI DJI_FC_SUBSCRIPTION_TOPIC_GIMBAL_ANGLES,
														 (uint8_t*)&gimbalAngle,
														 sizeof(gimbalAngle),
														 &timestamp) == _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
		{
			payload.YTFY = gimbalAngle.x;
			payload.YTHG = gimbalAngle.y;
			payload.YTPH = gimbalAngle.z;
		}

		if (_DJI T_DjiAircraftInfoBaseInfo aircraftInfo {};
			_DJI DjiAircraftInfo_GetBaseInfo(&aircraftInfo) == _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
		{
			payload.XH = aircraftTypeToString(aircraftInfo.aircraftType);
		}
		else
		{
			payload.XH = "N/A";
		}

		payload.CJ = "DJI";

		return payload;
	}

	void PSDKAdapter::quaternionToEulerAngle(const T_DjiFcSubscriptionQuaternion& q, double& roll, double& pitch, double& yaw) noexcept
	{
		double		sinr_cosp = 2 * (q.q0 * q.q1 + q.q2 * q.q3);
		double		cosr_cosp = 1 - 2 * (q.q1 * q.q1 + q.q2 * q.q2);
		roll				  = _STD atan2(sinr_cosp, cosr_cosp) * 180.0 / MATH_PI;

		double		sinp	  = 2 * (q.q0 * q.q2 - q.q3 * q.q1);
		if (_STD abs(sinp) >= 1)
		{
			pitch = _STD copysign(MATH_PI / 2, sinp) * 180.0 / MATH_PI;
		}
		else
		{
			pitch = _STD asin(sinp) * 180.0 / MATH_PI;
		}

		double	   siny_cosp = 2 * (q.q0 * q.q3 + q.q1 * q.q2);
		double	   cosy_cosp = 1 - 2 * (q.q2 * q.q2 + q.q3 * q.q3);
		yaw					 = _STD atan2(siny_cosp, cosy_cosp) * 180.0 / MATH_PI;
	}

	T_DjiReturnCode PSDKAdapter::takeoff(const protocol::TakeoffPayload& takeoffParams) const noexcept
	{
		try
		{
			if (plane::utils::isStandardProceduresEnabled())
			{
				T_DjiReturnCode returnCode { DjiFlightController_StartTakeoff() };
				if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
				{
					LOG_ERROR("PSDK API DjiFlightController_StartTakeoff() 调用失败, 错误码: 0x{:08X}", returnCode);
				}
				return returnCode;
			}
			else
			{
				LOG_WARN("环境变量 FULL_PSDK 未设置或不为 '1', 起飞操作被禁止。");
				return DJI_ERROR_SYSTEM_MODULE_CODE_NONSUPPORT;
			}
		}
		catch (...)
		{
			LOG_ERROR("PSDKAdapter::takeoff() 调用失败, 发生未知异常");
			return DJI_ERROR_SYSTEM_MODULE_CODE_UNKNOWN;
		}
	}

	T_DjiReturnCode PSDKAdapter::goHome(void) const noexcept
	{
		try
		{
			if (plane::utils::isStandardProceduresEnabled())
			{
				T_DjiReturnCode returnCode { DjiFlightController_StartGoHome() };
				if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
				{
					LOG_ERROR("PSDK API DjiFlightController_StartGoHome() 调用失败, 错误码: 0x{:08X}", returnCode);
				}
				return returnCode;
			}
			else
			{
				LOG_WARN("环境变量 FULL_PSDK 未设置或不为 '1', 返航操作被禁止。");
				return DJI_ERROR_SYSTEM_MODULE_CODE_NONSUPPORT;
			}
		}
		catch (...)
		{
			LOG_ERROR("PSDKAdapter::goHome() 调用失败, 发生未知异常");
			return DJI_ERROR_SYSTEM_MODULE_CODE_UNKNOWN;
		}
	}

	T_DjiReturnCode PSDKAdapter::hover(void) const noexcept
	{
		try
		{
			if (plane::utils::isStandardProceduresEnabled())
			{
				T_DjiReturnCode returnCode { DjiFlightController_ExecuteEmergencyBrakeAction() };
				if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
				{
					LOG_ERROR("PSDK API DjiFlightController_ExecuteEmergencyBrakeAction() 调用失败, 错误码: 0x{:08X}", returnCode);
				}
				return returnCode;
			}
			else
			{
				LOG_WARN("环境变量 FULL_PSDK 未设置或不为 '1', 悬停操作被禁止。");
				return DJI_ERROR_SYSTEM_MODULE_CODE_NONSUPPORT;
			}
		}
		catch (...)
		{
			LOG_ERROR("PSDKAdapter::hover() 调用失败, 发生未知异常");
			return DJI_ERROR_SYSTEM_MODULE_CODE_UNKNOWN;
		}
	}

	T_DjiReturnCode PSDKAdapter::land(void) const noexcept
	{
		try
		{
			if (plane::utils::isStandardProceduresEnabled())
			{
				T_DjiReturnCode returnCode { DjiFlightController_StartLanding() };
				if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
				{
					LOG_ERROR("PSDK API DjiFlightController_StartLanding() 调用失败, 错误码: 0x{:08X}", returnCode);
				}
				return returnCode;
			}
			else
			{
				LOG_WARN("环境变量 FULL_PSDK 未设置或不为 '1', 降落操作被禁止。");
				return DJI_ERROR_SYSTEM_MODULE_CODE_NONSUPPORT;
			}
		}
		catch (...)
		{
			LOG_ERROR("PSDKAdapter::land() 调用失败, 发生未知异常");
			return DJI_ERROR_SYSTEM_MODULE_CODE_UNKNOWN;
		}
	}

	T_DjiReturnCode PSDKAdapter::waypointV3MissionStart(_STD string_view kmzFilePath) const noexcept
	{
		try
		{
			if (plane::utils::isStandardProceduresEnabled())
			{
				LOG_INFO("PSDK Adapter: 调用 DjiTest_WaypointV3RunSampleWithKmzFilePath(...)");
				return DjiTest_WaypointV3RunSampleWithKmzFilePath(kmzFilePath.data());
			}
			else
			{
				LOG_WARN("环境变量 FULL_PSDK 未设置或不为 '1', 航点任务操作被禁止。");
				return DJI_ERROR_SYSTEM_MODULE_CODE_NONSUPPORT;
			}
		}
		catch (...)
		{
			LOG_ERROR("PSDKAdapter::waypointV3MissionStart() 调用失败, 发生未知异常");
			return DJI_ERROR_SYSTEM_MODULE_CODE_UNKNOWN;
		}
	}

	T_DjiReturnCode PSDKAdapter::setControlStrategy(int strategyCode) const noexcept
	{
		try
		{
			if (plane::utils::isStandardProceduresEnabled())
			{
				LOG_INFO("PSDK Adapter: 设置云台控制策略, 策略代码: {}", strategyCode);
				// ...
				return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
			}
			else
			{
				LOG_WARN("环境变量 FULL_PSDK 未设置或不为 '1', 云台控制策略切换操作被禁止。");
				return DJI_ERROR_SYSTEM_MODULE_CODE_NONSUPPORT;
			}
		}
		catch (...)
		{
			LOG_ERROR("PSDKAdapter::setControlStrategy() 调用失败, 发生未知异常");
			return DJI_ERROR_SYSTEM_MODULE_CODE_UNKNOWN;
		}
	}

	T_DjiReturnCode PSDKAdapter::flyCircleAroundPoint(const protocol::CircleFlyPayload& circleParams) const noexcept
	{
		try
		{
			if (plane::utils::isStandardProceduresEnabled())
			{
				LOG_INFO("PSDK Adapter: 调用智能环绕, 参数: lat={}, lon={}, alt={}, r={}, spd={}",
						 circleParams.WD,
						 circleParams.JD,
						 circleParams.GD,
						 circleParams.BJ,
						 circleParams.SD);
				// ...
				return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
			}
			else
			{
				LOG_WARN("环境变量 FULL_PSDK 未设置或不为 '1', 智能环绕操作被禁止。");
				return DJI_ERROR_SYSTEM_MODULE_CODE_NONSUPPORT;
			}
		}
		catch (...)
		{
			LOG_ERROR("PSDKAdapter::flyCircleAroundPoint() 调用失败, 发生未知异常");
			return DJI_ERROR_SYSTEM_MODULE_CODE_UNKNOWN;
		}
	}

	// ... 在这里实现所有其他 PSDK API 的封装 ...
} // namespace plane::services

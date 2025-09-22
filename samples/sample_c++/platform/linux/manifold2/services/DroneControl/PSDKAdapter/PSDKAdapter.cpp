#include "services/DroneControl/PSDKAdapter/PSDKAdapter.h"

#include "camera_manager/test_camera_manager_entry.h"
#include "data_transmission/test_data_transmission.h"
#include "dji_camera_manager.h"
#include "dji_flight_controller.h"
#include "dji_gimbal.h"
#include "fc_subscription/test_fc_subscription.h"
#include "widget/test_widget.h"
#include "widget/test_widget_speaker.h"

#include <camera_emu/test_payload_cam_emu_base.h>
#include <camera_emu/test_payload_cam_emu_media.h>
#include <dji_logger.h>
#include <flight_control/test_flight_control.h>
#include <flight_controller/test_flight_controller_entry.h>
#include <gimbal/test_gimbal_entry.hpp>
#include <gimbal_emu/test_payload_gimbal_emu.h>
#include <hms_manager/hms_manager_entry.h>
#include <liveview/test_liveview_entry.hpp>
#include <perception/test_lidar_entry.hpp>
#include <perception/test_perception_entry.hpp>
#include <perception/test_radar_entry.hpp>
#include <positioning/test_positioning.h>
#include <power_management/test_power_management.h>
#include <waypoint_v3/test_waypoint_v3.h>

#include "utils/EnvironmentCheck/EnvironmentCheck.h"
#include "utils/Logger/Logger.h"

namespace plane::services
{
	PSDKAdapter& PSDKAdapter::getInstance(void) noexcept
	{
		static PSDKAdapter instance {};
		return instance;
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

	T_DjiReturnCode PSDKAdapter::waypointV3MissionStart(std::string_view kmzFilePath) const noexcept
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

#include "services/drone_control/PSDKAdapter/PSDKAdapter.h"
#include "utils/Logger/Logger.h"

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

namespace plane::services
{
	PSDKAdapter& PSDKAdapter::getInstance(void) noexcept
	{
		static PSDKAdapter instance;
		return instance;
	}

	T_DjiReturnCode PSDKAdapter::takeoff(const protocol::TakeoffPayload& takeoffParams) const noexcept
	{
		T_DjiReturnCode returnCode = DjiFlightController_StartTakeoff();
		if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
		{
			LOG_ERROR("PSDK API DjiFlightController_StartTakeoff() 调用失败，错误码: 0x{:08X}", returnCode);
		}
		return returnCode;
	}

	T_DjiReturnCode PSDKAdapter::goHome(void) const noexcept
	{
		LOG_INFO("PSDK Adapter: 调用 DjiFlightController_StartGoHome()");
		return DjiFlightController_StartGoHome();
	}

	T_DjiReturnCode PSDKAdapter::hover(void) const noexcept
	{
		LOG_INFO("PSDK Adapter: 调用 DjiFlightController_CancelTask()");
		// 悬停通常是通过取消当前任务来实现的
		return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
	}

	T_DjiReturnCode PSDKAdapter::setControlStrategy(int strategyCode) const noexcept
	{
		LOG_INFO("PSDK Adapter: 调用 DjiGimbal_SetControlMode(...)"); // 伪代码
		// ...
		return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
	}

	T_DjiReturnCode PSDKAdapter::flyCircleAroundPoint(const protocol::CircleFlyPayload& circleParams) const noexcept
	{
		LOG_INFO("PSDK Adapter: 调用 DjiHotpoint_Start(...)"); // 伪代码
		// ...
		return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
	}

	// ... 在这里实现所有其他 PSDK API 的封装 ...
} // namespace plane::services

#include "application.hpp"

#include "camera_manager/test_camera_manager_entry.h"
#include "data_transmission/test_data_transmission.h"
#include "dji_logger.h"
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

#include "config/ConfigManager.h"
#include "services/mqtt/MQTTService.h"

int main(int argc, char** argv)
{
	Application					   application(argc, argv);
	char						   inputChar;
	T_DjiOsalHandler*			   osalHandler = DjiPlatform_GetOsalHandler();
	T_DjiReturnCode				   returnCode;
	T_DjiTestApplyHighPowerHandler applyHighPowerHandler;

start:
	std::cout << "\n"
			  << "| Available commands:                                                                              |\n"
			  << "| [0] Fc subscribe sample - subscribe quaternion and gps data                                      |\n"
			  << "| [1] Flight controller sample - you can control flying by PSDK                                    |\n"
			  << "| [2] Hms info manager sample - get health manger system info by language                          |\n"
			  << "| [a] Gimbal manager sample - you can control gimbal by PSDK                                       |\n"
			  << "| [c] Camera stream view sample - display the camera video stream                                  |\n"
			  << "| [d] Stereo vision view sample - display the stereo image                                         |\n"
			  << "| [e] Run camera manager sample - you can test camera's functions interactively                    |\n"
			  << "| [f] Start rtk positioning sample - you can receive rtk rtcm data when rtk signal is ok           |\n"
			  << "| [g] Request Lidar data sample - Request Lidar data and store the point cloud data as pcd files   |\n"
			  << "| [h] Request Radar data sample - Request radar data                                               |\n"
			  << std::endl;

	std::cin >> inputChar;
	switch (inputChar)
	{
		case '0':
		{
			DjiTest_FcSubscriptionRunSample();
			break;
		}
		case '1':
		{
			DjiUser_RunFlightControllerSample();
			break;
		}
		case '2':
		{
			DjiUser_RunHmsManagerSample();
			break;
		}
		case 'a':
		{
			DjiUser_RunGimbalManagerSample();
			break;
		}
		case 'c':
		{
			DjiUser_RunCameraStreamViewSample();
			break;
		}
		case 'd':
		{
			DjiUser_RunStereoVisionViewSample();
			break;
		}
		case 'e':
		{
			DjiUser_RunCameraManagerSample();
			break;
		}
		case 'f':
		{
			returnCode = DjiTest_PositioningStartService();
			if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
			{
				USER_LOG_ERROR("rtk positioning sample init error");
				break;
			}

			USER_LOG_INFO("Start rtk positioning sample successfully");
			break;
		}
		case 'g':
		{
			DjiUser_RunLidarDataSubscriptionSample();
			break;
		}
		case 'h':
		{
			DjiUser_RunRadarDataSubscriptionSample();
			break;
		}
		default:
		{
			break;
		}
	}

	osalHandler->TaskSleepMs(2000);

	goto start;
}

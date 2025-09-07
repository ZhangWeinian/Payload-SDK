/**
 ********************************************************************
 * @file    main.cpp
 * @brief
 *
 * @copyright (c) 2021 DJI. All rights reserved.
 *
 * All information contained herein is, and remains, the property of DJI.
 * The intellectual and technical concepts contained herein are proprietary
 * to DJI and may be covered by U.S. and foreign patents, patents in process,
 * and protected by trade secret or copyright law.  Dissemination of this
 * information, including but not limited to data and other proprietary
 * material(s) incorporated within the information, in any form, is strictly
 * prohibited without the express written consent of DJI.
 *
 * If you receive this source code without DJI’s authorization, you may not
 * further disseminate the information, and you must immediately remove the
 * source code and notify DJI of its removal. DJI reserves the right to pursue
 * legal actions against you for any loss(es) or damage(s) caused by your
 * failure to do so.
 *
 *********************************************************************
 */

/* Includes ------------------------------------------------------------------*/
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

/* Private constants ---------------------------------------------------------*/

/* Private types -------------------------------------------------------------*/

/* Private values -------------------------------------------------------------*/

/* Private functions declaration ---------------------------------------------*/

/* Exported functions definition ---------------------------------------------*/
int main(int argc, char** argv)
{
	Application					   application(argc, argv);
	char						   inputChar;
	T_DjiOsalHandler*			   osalHandler = DjiPlatform_GetOsalHandler();
	T_DjiReturnCode				   returnCode;
	T_DjiTestApplyHighPowerHandler applyHighPowerHandler;

start:
	std::cout << "\n"
			  << "| 可用命令：\n"
			  << "| [0] Fc 订阅样本 - 订阅四元数和 gps 数据\n"
			  << "| [1] 飞行控制器示例 - 可以通过 PSDK 控制飞行\n"
			  << "| [2] Hms 信息管理器示例 - 通过语言获取健康管理系统信息\n"
			  << "| [a] 云台管理器示例 - 可以通过 PSDK 控制云台\n"
			  << "| [c] 相机流视图示例 - 显示相机视频流\n"
			  << "| [d] 立体视觉视图示例 - 显示立体图像\n"
			  << "| [e] 运行相机管理器示例 - 可以交互式测试相机的功能\n"
			  << "| [f] 运行 RTK 定位示例 - 当 RTK 信号正常时，可以接收 RTK RTCM 数据\n"
			  << "| [g] 请求激光雷达数据示例 - 请求激光雷达数据并将点云数据存储为 PCD 文件\n"
			  << "| [h] 请求雷达数据示例 - 请求雷达数据\n"
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

/* Private functions definition-----------------------------------------------*/

/****************** (C) COPYRIGHT DJI Innovations *****END OF FILE****/

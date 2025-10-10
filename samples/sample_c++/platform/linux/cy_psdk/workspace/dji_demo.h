// cy_psdk/my_dji/dji_demo.h

#pragma once

#include "application.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

#include "camera_manager/test_camera_manager_entry.h"
#include "data_transmission/test_data_transmission.h"
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

namespace plane::dji_demo
{
	void runDjiApplication(int argc, char* argv[])
	{
		Application					   application(argc, argv);
		char						   inputChar {};
		T_DjiOsalHandler*			   osalHandler = DjiPlatform_GetOsalHandler();
		T_DjiReturnCode				   returnCode {};
		T_DjiTestApplyHighPowerHandler applyHighPowerHandler {};

start:
		std::cout << "\n"
				  << "| 可用命令:\n"
				  << "| [0] 飞控数据订阅示例 - 订阅四元数和 GPS 数据\n"
				  << "| [1] 飞行控制器示例 - 通过 PSDK 控制飞行\n"
				  << "| [2] 健康管理系统信息示例 - 按语言获取健康管理系统信息\n"
				  << "| [a] 云台管理器示例 - 通过 PSDK 控制云台\n"
				  << "| [c] 相机码流查看示例 - 显示相机视频流\n"
				  << "| [d] 双目视觉查看示例 - 显示双目图像\n"
				  << "| [e] 运行相机管理器示例 - 交互式地测试相机功能\n"
				  << "| [f] 启动 RTK 定位示例 - 当 RTK 信号正常时, 接收 RTK RTCM 数据\n"
				  << "| [g] 请求激光雷达数据示例 - 请求激光雷达数据并将点云数据存储为 pcd 文件\n"
				  << "| [h] 请求毫米波雷达数据示例 - 请求毫米波雷达数据\n"
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
					USER_LOG_ERROR("RTK 定位样本初始化错误");
				}
				else
				{
					USER_LOG_INFO("成功启动 RTK 定位样本");
				}
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
} // namespace plane::dji_demo

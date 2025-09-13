#include "dji_logger.h"
#include "test_flight_controller_command_flying.h"
#include "test_flight_controller_entry.h"
#include <flight_control/test_flight_control.h>
#include <interest_point/test_interest_point.h>
#include <waypoint_v2/test_waypoint_v2.h>
#include <waypoint_v3/test_waypoint_v3.h>
#include <iostream>

void DjiUser_RunFlightControllerSample(void)
{
	T_DjiOsalHandler* osalHandler = DjiPlatform_GetOsalHandler();
	char			  inputSelectSample;

start:
	osalHandler->TaskSleepMs(100);

	std::cout << "\n"
			  << "| 可用命令:\n"
			  << "| [0] 飞行控制器示例 - 使用键盘控制飞行\n"
			  << "| [1] 飞行控制器示例 - 起飞与降落\n"
			  << "| [2] 飞行控制器示例 - 起飞 -> 位置控制 -> 降落\n"
			  << "| [3] 飞行控制器示例 - 起飞 -> 返航 -> 强制降落\n"
			  << "| [4] 飞行控制器示例 - 起飞 -> 速度控制 -> 降落\n"
			  << "| [5] 飞行控制器示例 - 紧急停止飞行 (Arrest Flying)\n"
			  << "| [6] 飞行控制器示例 - 设置与获取参数\n"
			  << "| [7] 航点 2.0 示例 - 通过设置运行航线任务 (仅支持 M300 RTK)\n"
			  << "| [8] 航点 3.0 示例 - 通过 KMZ 文件运行航线任务 (不支持 M300 RTK)\n"
			  << "| [9] 兴趣点示例 - 通过设置运行兴趣点任务 (仅支持 M3E/M3T)\n"
			  << "| [a] EU-C6 FTS 触发示例 - 接收 FTS 回调以触发降落伞功能 (仅支持 M3D/M3DT)\n"
			  << "| [b] 慢速转桨示例 (仅支持 M400)\n"
			  << "| [c] 选择 FTS PWM 触发位置 (支持 M4/M4T/M4D/M4TD)\n"
			  << "| [d] 选择 FTS PWM 触发位置 (支持 M400)\n"
			  << std::endl;

	std::cin >> inputSelectSample;
	switch (inputSelectSample)
	{
		case '0':
		{
			DjiUser_RunFlightControllerCommandFlyingSample();
			goto start;
		}
		case '1':
		{
			DjiTest_FlightControlRunSample(E_DJI_TEST_FLIGHT_CTRL_SAMPLE_SELECT_TAKE_OFF_LANDING);
			goto start;
		}
		case '2':
		{
			DjiTest_FlightControlRunSample(E_DJI_TEST_FLIGHT_CTRL_SAMPLE_SELECT_TAKE_OFF_POSITION_CTRL_LANDING);
			goto start;
		}
		case '3':
		{
			DjiTest_FlightControlRunSample(E_DJI_TEST_FLIGHT_CTRL_SAMPLE_SELECT_TAKE_OFF_GO_HOME_FORCE_LANDING);
			goto start;
		}
		case '4':
		{
			DjiTest_FlightControlRunSample(E_DJI_TEST_FLIGHT_CTRL_SAMPLE_SELECT_TAKE_OFF_VELOCITY_CTRL_LANDING);
			goto start;
		}
		case '5':
		{
			DjiTest_FlightControlRunSample(E_DJI_TEST_FLIGHT_CTRL_SAMPLE_SELECT_ARREST_FLYING);
			goto start;
		}
		case '6':
		{
			DjiTest_FlightControlRunSample(E_DJI_TEST_FLIGHT_CTRL_SAMPLE_SELECT_SET_GET_PARAM);
			goto start;
		}
		case '7':
		{
			DjiTest_WaypointV2RunSample();
			break;
		}
		case '8':
		{
			DjiTest_WaypointV3RunSample();
			break;
		}
		case '9':
		{
			DjiTest_InterestPointRunSample();
			break;
		}
		case 'a':
		{
			DjiTest_FlightControlRunSample(E_DJI_TEST_FLIGHT_CTRL_SAMPLE_SELECT_FTS_TRIGGER);
			break;
		}
		case 'b':
		{
			DjiTest_FlightControlRunSample(E_DJI_TEST_FLIGHT_CTRL_SAMPLE_SELECT_SLOW_ROTATE_BLADE);
			break;
		}
		case 'c':
		{
			DjiTest_FlightControlFtsPwmTriggerSample(DJI_MOUNT_POSITION_EXTENSION_PORT, "DJI_MOUNT_POSITION_EXTENSION_PORT");
			// or DJI_MOUNT_POSITION_EXTENSION_LITE_PORT
			DjiTest_FlightControlFtsPwmTriggerSample(DJI_MOUNT_POSITION_EXTENSION_LITE_PORT, "DJI_MOUNT_POSITION_EXTENSION_LITE_PORT");
			break;
		}
		case 'd': // for m400
		{
			DjiTest_FlightControlFtsPwmTriggerSample(DJI_MOUNT_POSITION_EXTENSION_PORT_V2_NO4, "DJI_MOUNT_POSITION_EXTENSION_PORT_V2_NO4");
			break;
		}
		case 'q':
		{
			break;
		}
		default:
		{
			USER_LOG_ERROR("Input command is invalid");
			goto start;
		}
	}
}

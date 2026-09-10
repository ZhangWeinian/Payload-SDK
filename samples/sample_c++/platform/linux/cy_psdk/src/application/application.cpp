/**
 ********************************************************************
 * @file    application.cpp
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
#include "define.h" // cy_psdk 全局命名宏 (_STD / _CSTD / _DJI 等)
#include "dji_sdk_app_info.h"
#include "dji_sdk_config.h"
#include <dji_aircraft_info.h>
#include <dji_core.h>
#include <dji_logger.h>
#include <dji_platform.h>
#include <csignal>

#include "../common/osal/osal.h"
#include "../common/osal/osal_fs.h"
#include "../common/osal/osal_socket.h"
#include "../hal/hal_i2c.h"
#include "../hal/hal_network.h"
#include "../hal/hal_uart.h"
#include "../hal/hal_usb_bulk.h"

#include "data_transmission/test_data_transmission.h"
#include "widget/test_widget.h"
#include "widget/test_widget_speaker.h"
#include <camera_emu/test_payload_cam_emu_base.h>
#include <camera_emu/test_payload_cam_emu_media.h>
#include <gimbal_emu/test_payload_gimbal_emu.h>
#include <power_management/test_power_management.h>

/* Private constants ---------------------------------------------------------*/
#define DJI_LOG_PATH				 "Logs/DJI"
#define DJI_LOG_INDEX_FILE_NAME		 "Logs/index"
#define DJI_LOG_FOLDER_NAME			 "Logs"
#define DJI_LOG_PATH_MAX_SIZE		 (128)
#define DJI_LOG_FOLDER_NAME_MAX_SIZE (32)
#define DJI_SYSTEM_CMD_STR_MAX_SIZE	 (64)
#define DJI_LOG_MAX_COUNT			 (10)

#define USER_UTIL_UNUSED(x)			 ((x) = (x))
#define USER_UTIL_MIN(a, b)			 (((a) < (b)) ? (a) : (b))
#define USER_UTIL_MAX(a, b)			 (((a) > (b)) ? (a) : (b))

/* Private types -------------------------------------------------------------*/

/* Private values -------------------------------------------------------------*/
static _CSTD FILE* s_djiLogFile;
static _CSTD FILE* s_djiLogFileCnt;

/* Private functions declaration ---------------------------------------------*/
static void					DjiUser_NormalExitHandler(int signalNum);
static _DJI T_DjiReturnCode DjiTest_HighPowerApplyPinInit();
static _DJI T_DjiReturnCode DjiTest_WriteHighPowerApplyPin(_DJI E_DjiPowerManagementPinState pinState);

/* Exported functions definition ---------------------------------------------*/
Application::Application(int /*argc*/, char** /*argv*/)
{
	Application::DjiUser_SetupEnvironment();
	Application::DjiUser_ApplicationStart();

	_DJI Osal_TaskSleepMs(3000);
}

Application::~Application() = default;

/* Private functions definition-----------------------------------------------*/
void Application::DjiUser_SetupEnvironment()
{
	_DJI T_DjiReturnCode					   returnCode;
	_DJI T_DjiOsalHandler					   osalHandler	  = { 0 };
	_DJI T_DjiHalUartHandler				   uartHandler	  = { 0 };
	_DJI T_DjiHalUsbBulkHandler				   usbBulkHandler = { 0 };
	_DJI T_DjiLoggerConsole					   printConsole;
	_DJI T_DjiLoggerConsole					   localRecordConsole;
	_DJI T_DjiFileSystemHandler				   fileSystemHandler = { 0 };
	_DJI T_DjiSocketHandler					   socketHandler { 0 };
	_DJI T_DjiHalNetworkHandler				   networkHandler = { 0 };
	_DJI T_DjiHalI2cHandler					   i2CHandler	  = { 0 };

	networkHandler.NetworkInit								  = _DJI		  HalNetWork_Init;
	networkHandler.NetworkDeInit							  = _DJI		HalNetWork_DeInit;
	networkHandler.NetworkGetDeviceInfo						  = _DJI HalNetWork_GetDeviceInfo;

	socketHandler.Socket									  = _DJI				Osal_Socket;
	socketHandler.Bind										  = _DJI				  Osal_Bind;
	socketHandler.Close										  = _DJI				 Osal_Close;
	socketHandler.UdpSendData								  = _DJI		   Osal_UdpSendData;
	socketHandler.UdpRecvData								  = _DJI		   Osal_UdpRecvData;
	socketHandler.TcpListen									  = _DJI			 Osal_TcpListen;
	socketHandler.TcpAccept									  = _DJI			 Osal_TcpAccept;
	socketHandler.TcpConnect								  = _DJI			Osal_TcpConnect;
	socketHandler.TcpSendData								  = _DJI		   Osal_TcpSendData;
	socketHandler.TcpRecvData								  = _DJI		   Osal_TcpRecvData;

	osalHandler.TaskCreate									  = _DJI			  Osal_TaskCreate;
	osalHandler.TaskDestroy									  = _DJI			 Osal_TaskDestroy;
	osalHandler.TaskSleepMs									  = _DJI			 Osal_TaskSleepMs;
	osalHandler.MutexCreate									  = _DJI			 Osal_MutexCreate;
	osalHandler.MutexDestroy								  = _DJI			Osal_MutexDestroy;
	osalHandler.MutexLock									  = _DJI			   Osal_MutexLock;
	osalHandler.MutexUnlock									  = _DJI			 Osal_MutexUnlock;
	osalHandler.SemaphoreCreate								  = _DJI		 Osal_SemaphoreCreate;
	osalHandler.SemaphoreDestroy							  = _DJI		Osal_SemaphoreDestroy;
	osalHandler.SemaphoreWait								  = _DJI		   Osal_SemaphoreWait;
	osalHandler.SemaphoreTimedWait							  = _DJI	  Osal_SemaphoreTimedWait;
	osalHandler.SemaphorePost								  = _DJI		   Osal_SemaphorePost;
	osalHandler.Malloc										  = _DJI				  Osal_Malloc;
	osalHandler.Free										  = _DJI					Osal_Free;
	osalHandler.GetTimeMs									  = _DJI			   Osal_GetTimeMs;
	osalHandler.GetTimeUs									  = _DJI			   Osal_GetTimeUs;
	osalHandler.GetRandomNum								  = _DJI			Osal_GetRandomNum;

	printConsole.func										  = DjiUser_PrintConsole;
	printConsole.consoleLevel								  = _DJI DJI_LOGGER_CONSOLE_LOG_LEVEL_INFO;
	printConsole.isSupportColor								  = true;

	localRecordConsole.consoleLevel							  = _DJI DJI_LOGGER_CONSOLE_LOG_LEVEL_DEBUG;
	localRecordConsole.func									  = DjiUser_LocalWrite;
	localRecordConsole.isSupportColor						  = false;

	uartHandler.UartInit									  = _DJI				HalUart_Init;
	uartHandler.UartDeInit									  = _DJI			  HalUart_DeInit;
	uartHandler.UartWriteData								  = _DJI		   HalUart_WriteData;
	uartHandler.UartReadData								  = _DJI			HalUart_ReadData;
	uartHandler.UartGetStatus								  = _DJI		   HalUart_GetStatus;
	uartHandler.UartGetDeviceInfo							  = _DJI	   HalUart_GetDeviceInfo;
	i2CHandler.I2cInit										  = _DJI				  HalI2c_Init;
	i2CHandler.I2cDeInit									  = _DJI				HalI2c_DeInit;
	i2CHandler.I2cWriteData									  = _DJI			 HalI2c_WriteData;
	i2CHandler.I2cReadData									  = _DJI			  HalI2c_ReadData;

	usbBulkHandler.UsbBulkInit								  = _DJI		  HalUsbBulk_Init;
	usbBulkHandler.UsbBulkDeInit							  = _DJI		HalUsbBulk_DeInit;
	usbBulkHandler.UsbBulkWriteData							  = _DJI	 HalUsbBulk_WriteData;
	usbBulkHandler.UsbBulkReadData							  = _DJI	  HalUsbBulk_ReadData;
	usbBulkHandler.UsbBulkGetDeviceInfo						  = _DJI HalUsbBulk_GetDeviceInfo;

	fileSystemHandler.FileOpen = _DJI Osal_FileOpen, fileSystemHandler.FileClose = _DJI Osal_FileClose,
	fileSystemHandler.FileWrite = _DJI Osal_FileWrite, fileSystemHandler.FileRead = _DJI Osal_FileRead,
	fileSystemHandler.FileSync = _DJI Osal_FileSync, fileSystemHandler.FileSeek = _DJI Osal_FileSeek,
	fileSystemHandler.DirOpen = _DJI Osal_DirOpen, fileSystemHandler.DirClose = _DJI Osal_DirClose,
	fileSystemHandler.DirRead = _DJI Osal_DirRead, fileSystemHandler.Mkdir = _DJI Osal_Mkdir, fileSystemHandler.Unlink = _DJI Osal_Unlink,
	fileSystemHandler.Rename = _DJI Osal_Rename, fileSystemHandler.Stat = _DJI Osal_Stat,
	returnCode = _DJI DjiPlatform_RegOsalHandler(&osalHandler);
	if (returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
	{
		throw _STD runtime_error("Register osal handler error.");
	}

	// 注册 I2C HAL (Raspberry Pi 平台支持)
	returnCode = _DJI DjiPlatform_RegHalI2cHandler(&i2CHandler);
	if (returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
	{
		throw _STD runtime_error("Register hal i2c handler error.");
	}

// 按硬件连接方式注册 HAL (与 raspberry_pi 官方样例对齐)
#if (CONFIG_HARDWARE_CONNECTION == DJI_USE_UART_AND_USB_BULK_DEVICE)
	returnCode = _DJI DjiPlatform_RegHalUartHandler(&uartHandler);
	if (returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
	{
		throw _STD runtime_error("Register hal uart handler error.");
	}

	returnCode = _DJI DjiPlatform_RegHalUsbBulkHandler(&usbBulkHandler);
	if (returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
	{
		throw _STD runtime_error("Register hal usb bulk handler error.");
	}
#elif (CONFIG_HARDWARE_CONNECTION == DJI_USE_UART_AND_NETWORK_DEVICE)
	returnCode = _DJI DjiPlatform_RegHalUartHandler(&uartHandler);
	if (returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
	{
		throw _STD runtime_error("Register hal uart handler error.");
	}

	returnCode = _DJI DjiPlatform_RegHalNetworkHandler(&networkHandler);
	if (returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
	{
		throw _STD runtime_error("Register hal network handler error");
	}
#elif (CONFIG_HARDWARE_CONNECTION == DJI_USE_ONLY_USB_BULK_DEVICE)
	returnCode = _DJI DjiPlatform_RegHalUsbBulkHandler(&usbBulkHandler);
	if (returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
	{
		throw _STD runtime_error("Register hal usb bulk handler error.");
	}
#elif (CONFIG_HARDWARE_CONNECTION == DJI_USE_ONLY_NETWORK_DEVICE)
	returnCode = _DJI DjiPlatform_RegHalNetworkHandler(&networkHandler);
	if (returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
	{
		throw _STD runtime_error("Register hal network handler error");
	}

	// Attention: if you want to use camera stream view function, please uncomment it.
	returnCode = _DJI DjiPlatform_RegSocketHandler(&socketHandler);
	if (returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
	{
		throw _STD runtime_error("register osal socket handler error");
	}

#elif (CONFIG_HARDWARE_CONNECTION == DJI_USE_ONLY_UART)
	/*!< Attention: Only use uart hardware connection. */
	returnCode = _DJI DjiPlatform_RegHalUartHandler(&uartHandler);
	if (returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
	{
		throw _STD runtime_error("Register hal uart handler error.");
	}
#endif

	// Attention: if you want to use camera stream view function, please uncomment it.
	returnCode = _DJI DjiPlatform_RegSocketHandler(&socketHandler);
	if (returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
	{
		throw _STD runtime_error("register osal socket handler error");
	}

	returnCode = _DJI DjiPlatform_RegFileSystemHandler(&fileSystemHandler);
	if (returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
	{
		throw _STD runtime_error("Register osal filesystem handler error.");
	}

	// if (DjiUser_LocalWriteFsInit(DJI_LOG_PATH) != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
	//   throw _STD runtime_error("File system init error.");
	// }

	// returnCode = _DJI DjiLogger_AddConsole(&printConsole);
	// if (returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
	//   throw _STD runtime_error("Add printf console error.");
	// }

	// returnCode = _DJI DjiLogger_AddConsole(&localRecordConsole);
	// if (returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
	//   throw _STD runtime_error("Add printf console error.");
	// }
}

void Application::DjiUser_ApplicationStart()
{
	_DJI T_DjiUserInfo			   userInfo;
	_DJI T_DjiReturnCode		   returnCode;
	_DJI T_DjiAircraftInfoBaseInfo aircraftInfoBaseInfo;
	_DJI T_DjiFirmwareVersion	   firmwareVersion = {
		.majorVersion  = 1,
		.minorVersion  = 0,
		.modifyVersion = 0,
		.debugVersion  = 0,
	};

	// attention: when the program is hand up ctrl-c will generate the coredump file
	_CSTD signal(SIGTERM, DjiUser_NormalExitHandler);

	returnCode = DjiUser_FillInUserInfo(&userInfo);
	if (returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
	{
		throw _STD runtime_error("Fill user info error, please check user info config.");
	}

	returnCode = _DJI DjiCore_SetFirmwareVersion(firmwareVersion);
	if (returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
	{
		throw _STD runtime_error("Set firmware version error.");
	}

	returnCode = _DJI DjiCore_SetSerialNumber("PSDK12345678XX");
	if (returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
	{
		throw _STD runtime_error("Set serial number error");
	}

	returnCode = _DJI DjiCore_Init(&userInfo);
	if (returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
	{
		_CSTD	   sleep(1);
		throw _STD runtime_error("Core init error.");
	}

	returnCode = _DJI DjiAircraftInfo_GetBaseInfo(&aircraftInfoBaseInfo);
	if (returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
	{
		throw _STD runtime_error("Get aircraft base info error.");
	}

	if (aircraftInfoBaseInfo.mountPosition != _DJI DJI_MOUNT_POSITION_EXTENSION_PORT &&
		aircraftInfoBaseInfo.djiAdapterType != _DJI DJI_SDK_ADAPTER_TYPE_EPORT_V2_RIBBON_CABLE &&
		aircraftInfoBaseInfo.djiAdapterType != _DJI DJI_SDK_ADAPTER_TYPE_SKYPORT_V3)
	{
		throw _STD runtime_error("Please run this sample on extension port or skyport v3.");
	}

	returnCode = _DJI DjiCore_SetAlias("PSDK_APPALIAS");
	if (returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
	{
		throw _STD runtime_error("Set alias error.");
	}

#ifdef CONFIG_MODULE_SAMPLE_CAMERA_EMU_ON
	returnCode = _DJI DjiTest_CameraEmuBaseStartService();
	if (returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
	{
		USER_LOG_ERROR("camera emu common init error");
	}
#endif

#ifdef CONFIG_MODULE_SAMPLE_CAMERA_MEDIA_ON
	returnCode = _DJI DjiTest_CameraEmuMediaStartService();
	if (returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
	{
		USER_LOG_ERROR("camera emu media init error");
	}
#endif

#ifdef CONFIG_MODULE_SAMPLE_GIMBAL_EMU_ON
	returnCode = _DJI DjiTest_GimbalStartService();
	if (returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
	{
		USER_LOG_ERROR("psdk gimbal init error");
	}
#endif

#ifdef CONFIG_MODULE_SAMPLE_WIDGET_ON
	returnCode = _DJI DjiTest_WidgetStartService();
	if (returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
	{
		USER_LOG_ERROR("widget sample init error");
	}
#endif

#ifdef CONFIG_MODULE_SAMPLE_WIDGET_SPEAKER_ON
	returnCode = _DJI DjiTest_WidgetSpeakerStartService();
	if (returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
	{
		USER_LOG_ERROR("widget speaker test init error");
	}
#endif

#ifdef CONFIG_MODULE_SAMPLE_POWER_MANAGEMENT_ON
	_DJI T_DjiTestApplyHighPowerHandler applyHighPowerHandler = {
		.pinInit  = DjiTest_HighPowerApplyPinInit,
		.pinWrite = DjiTest_WriteHighPowerApplyPin,
	};

	returnCode = _DJI DjiTest_RegApplyHighPowerHandler(&applyHighPowerHandler);
	if (returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
	{
		USER_LOG_ERROR("regsiter apply high power handler error");
	}

	returnCode = _DJI DjiTest_PowerManagementStartService();
	if (returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
	{
		USER_LOG_ERROR("power management init error");
	}
#endif

#ifdef CONFIG_MODULE_SAMPLE_DATA_TRANSMISSION_ON
	returnCode = _DJI DjiTest_DataTransmissionStartService();
	if (returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
	{
		USER_LOG_ERROR("widget sample init error");
	}
#endif

	returnCode = _DJI DjiCore_ApplicationStart();
	if (returnCode != _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
	{
		throw _STD runtime_error("Start sdk application error.");
	}

	USER_LOG_INFO("Application start.");
}

_DJI T_DjiReturnCode Application::DjiUser_PrintConsole(const uint8_t* data, uint16_t dataLen)
{
	_CSTD		printf("%s", data);

	return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

_DJI T_DjiReturnCode Application::DjiUser_LocalWrite(const uint8_t* data, uint16_t dataLen)
{
	int32_t realLen;

	if (s_djiLogFile == nullptr)
	{
		return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_UNKNOWN;
	}

	realLen = _CSTD fwrite(data, 1, dataLen, s_djiLogFile);
	_CSTD			fflush(s_djiLogFile);
	if (realLen == dataLen)
	{
		return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
	}
	else
	{
		return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_UNKNOWN;
	}
}

_DJI T_DjiReturnCode Application::DjiUser_FillInUserInfo(_DJI T_DjiUserInfo* userInfo)
{
	_CSTD memset(userInfo->appName, 0, sizeof(userInfo->appName));
	_CSTD memset(userInfo->appId, 0, sizeof(userInfo->appId));
	_CSTD memset(userInfo->appKey, 0, sizeof(userInfo->appKey));
	_CSTD memset(userInfo->appLicense, 0, sizeof(userInfo->appLicense));
	_CSTD memset(userInfo->developerAccount, 0, sizeof(userInfo->developerAccount));
	_CSTD memset(userInfo->baudRate, 0, sizeof(userInfo->baudRate));

	if (_CSTD strlen(USER_APP_NAME) >= sizeof(userInfo->appName) || _CSTD strlen(USER_APP_ID) > sizeof(userInfo->appId) ||
		_CSTD strlen(USER_APP_KEY) > sizeof(userInfo->appKey) || _CSTD strlen(USER_APP_LICENSE) > sizeof(userInfo->appLicense) ||
		_CSTD strlen(USER_DEVELOPER_ACCOUNT) >= sizeof(userInfo->developerAccount) || _CSTD strlen(USER_BAUD_RATE) > sizeof(userInfo->baudRate))
	{
		USER_LOG_ERROR("Length of user information string is beyond limit. Please check.");
		return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
	}

	if (!_CSTD strcmp(USER_APP_NAME, "your_app_name") || !_CSTD strcmp(USER_APP_ID, "your_app_id") ||
		!_CSTD strcmp(USER_APP_KEY, "your_app_key") || !_CSTD strcmp(USER_BAUD_RATE, "your_app_license") ||
		!_CSTD strcmp(USER_DEVELOPER_ACCOUNT, "your_developer_account") || !_CSTD strcmp(USER_BAUD_RATE, "your_baud_rate"))
	{
		USER_LOG_ERROR(
			"Please fill in correct user information to 'samples/sample_c++/platform/linux/cy_psdk/src/application/dji_sdk_app_info.h' file."
		);
		return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
	}

	_CSTD		strncpy(userInfo->appName, USER_APP_NAME, sizeof(userInfo->appName) - 1);
	_CSTD		memcpy(userInfo->appId, USER_APP_ID, USER_UTIL_MIN(sizeof(userInfo->appId), _CSTD strlen(USER_APP_ID)));
	_CSTD		memcpy(userInfo->appKey, USER_APP_KEY, USER_UTIL_MIN(sizeof(userInfo->appKey), _CSTD strlen(USER_APP_KEY)));
	_CSTD		memcpy(userInfo->appLicense, USER_APP_LICENSE, USER_UTIL_MIN(sizeof(userInfo->appLicense), _CSTD strlen(USER_APP_LICENSE)));
	_CSTD		memcpy(userInfo->baudRate, USER_BAUD_RATE, USER_UTIL_MIN(sizeof(userInfo->baudRate), _CSTD strlen(USER_BAUD_RATE)));
	_CSTD		strncpy(userInfo->developerAccount, USER_DEVELOPER_ACCOUNT, sizeof(userInfo->developerAccount) - 1);

	return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

_DJI T_DjiReturnCode Application::DjiUser_LocalWriteFsInit(const char* path)
{
	_DJI T_DjiReturnCode djiReturnCode = _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
	char									  filePath[DJI_LOG_PATH_MAX_SIZE];
	char									  systemCmd[DJI_SYSTEM_CMD_STR_MAX_SIZE];
	char									  folderName[DJI_LOG_FOLDER_NAME_MAX_SIZE];
	_CSTD time_t currentTime				  = _CSTD time(nullptr);
	struct tm* localTime					  = _CSTD localtime(&currentTime);
	uint16_t					 logFileIndex = 0;
	uint16_t					 currentLogFileIndex;
	uint8_t						 ret;

	if (localTime == nullptr)
	{
		_CSTD		printf("Get local time error.\r\n");
		return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
	}

	if (_CSTD access(DJI_LOG_FOLDER_NAME, F_OK) != 0)
	{
		_CSTD		sprintf(folderName, "mkdir %s", DJI_LOG_FOLDER_NAME);
		ret = _CSTD system(folderName);
		if (ret != 0)
		{
			_CSTD		printf("Create new log folder error, ret:%d.\r\n", ret);
			return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
		}
	}

	s_djiLogFileCnt = _CSTD fopen(DJI_LOG_INDEX_FILE_NAME, "rb+");
	if (s_djiLogFileCnt == nullptr)
	{
		s_djiLogFileCnt = _CSTD fopen(DJI_LOG_INDEX_FILE_NAME, "wb+");
		if (s_djiLogFileCnt == nullptr)
		{
			return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
		}
	}
	else
	{
		ret = _CSTD fseek(s_djiLogFileCnt, 0, SEEK_SET);
		if (ret != 0)
		{
			_CSTD		printf("Seek log count file error, ret: %d, errno: %d.\r\n", ret, errno);
			return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
		}

		ret = _CSTD fread((uint16_t*)&logFileIndex, 1, sizeof(uint16_t), s_djiLogFileCnt);
		if (ret != sizeof(uint16_t))
		{
			_CSTD printf("Read log file index error.\r\n");
		}
	}

	currentLogFileIndex = logFileIndex;
	logFileIndex++;

	ret = _CSTD fseek(s_djiLogFileCnt, 0, SEEK_SET);
	if (ret != 0)
	{
		_CSTD		printf("Seek log file error, ret: %d, errno: %d.\r\n", ret, errno);
		return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
	}

	ret = _CSTD fwrite((uint16_t*)&logFileIndex, 1, sizeof(uint16_t), s_djiLogFileCnt);
	if (ret != sizeof(uint16_t))
	{
		_CSTD		printf("Write log file index error.\r\n");
		_CSTD		fclose(s_djiLogFileCnt);
		return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
	}

	_CSTD fclose(s_djiLogFileCnt);

	_CSTD sprintf(
		filePath,
		"%s_%04d_%04d%02d%02d_%02d-%02d-%02d.log",
		path,
		currentLogFileIndex,
		localTime->tm_year + 1900,
		localTime->tm_mon + 1,
		localTime->tm_mday,
		localTime->tm_hour,
		localTime->tm_min,
		localTime->tm_sec
	);

	s_djiLogFile = _CSTD fopen(filePath, "wb+");
	if (s_djiLogFile == nullptr)
	{
		USER_LOG_ERROR("Open filepath time error.");
		return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
	}

	if (logFileIndex >= DJI_LOG_MAX_COUNT)
	{
		_CSTD		sprintf(systemCmd, "rm -rf %s_%04d*.log", path, currentLogFileIndex - DJI_LOG_MAX_COUNT);
		ret = _CSTD system(systemCmd);
		if (ret != 0)
		{
			_CSTD		printf("Remove file error, ret:%d.\r\n", ret);
			return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
		}
	}

	_CSTD sprintf(systemCmd, "ln -sfrv %s " DJI_LOG_FOLDER_NAME "/latest.log", filePath);
	int rc = _CSTD system(systemCmd);
	if (rc != 0)
	{
		return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
	}
	return djiReturnCode;
}

static void DjiUser_NormalExitHandler(int signalNum)
{
	USER_UTIL_UNUSED(signalNum);
	_CSTD exit(0);
}

static _DJI T_DjiReturnCode DjiTest_HighPowerApplyPinInit()
{
	return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

static _DJI T_DjiReturnCode DjiTest_WriteHighPowerApplyPin(_DJI E_DjiPowerManagementPinState pinState)
{
	// attention: please pull up the HWPR pin state by hardware.
	return _DJI DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

/****************** (C) COPYRIGHT DJI Innovations *****END OF FILE****/

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
#include "../manager/psdk/PSDKManager.h" // PSDK 日志重定向到 spdlog
#include "../utils/log_util/Logger.h"    // LOG_* (spdlog, 与 DJI 日志重定向无关)

#include "define.h"                      // cy_psdk 全局命名宏 (::std::/ ::/ ::等)

#include "application.hpp"

#include "config/ConfigManager.h"

#include "dji_sdk_app_info.h"
#include "dji_sdk_config.h"
#include <dji_aircraft_info.h>
#include <dji_core.h>
#include <dji_logger.h>
#include <dji_platform.h>
#include <csignal>
#include <fstream>
#include <string>

#include "../../../common/osal/osal.h"
#include "../../../common/osal/osal_fs.h"
#include "../../../common/osal/osal_socket.h"
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
#define DJI_LOG_PATH                 "Logs/DJI"
#define DJI_LOG_INDEX_FILE_NAME      "Logs/index"
#define DJI_LOG_FOLDER_NAME          "Logs"
#define DJI_LOG_PATH_MAX_SIZE        (128)
#define DJI_LOG_FOLDER_NAME_MAX_SIZE (32)
#define DJI_SYSTEM_CMD_STR_MAX_SIZE  (64)
#define DJI_LOG_MAX_COUNT            (10)

// USB Bulk: "飞机(USB Host)是否已配置我们" 状态文件, 由 usb_bulk_config.sh 启动的
// usb_bulk_event_watcher 写入 (监听 ep0 的 FUNCTIONFS_ENABLE 事件);
// 路径须与 usb_bulk_config.sh 的 HOLDER_STATE 一致
#define USB_BULK_HOST_STATE_FILE "/run/usb_bulk_holder.state"

#define USER_UTIL_UNUSED(x)      ((x) = (x))
#define USER_UTIL_MIN(a, b)      (((a) < (b)) ? (a) : (b))
#define USER_UTIL_MAX(a, b)      (((a) > (b)) ? (a) : (b))

/* Private types -------------------------------------------------------------*/

/* Private values -------------------------------------------------------------*/
static ::FILE* s_djiLogFile;
static ::FILE* s_djiLogFileCnt;

/* Private functions declaration ---------------------------------------------*/
static void              DjiUser_NormalExitHandler(int signalNum);
static bool              DjiUser_IsUsbBulkHostConfigured();
static ::T_DjiReturnCode DjiTest_HighPowerApplyPinInit();
static ::T_DjiReturnCode DjiTest_WriteHighPowerApplyPin(::E_DjiPowerManagementPinState pinState);

/* Exported functions definition ---------------------------------------------*/
Application::Application(int /*argc*/, char** /*argv*/)
{
    Application::DjiUser_SetupEnvironment();
    Application::DjiUser_ApplicationStart();

    ::Osal_TaskSleepMs(3000);
}

Application::~Application() = default;

/* Private functions definition-----------------------------------------------*/

/* 判断飞机 (USB Host) 是否已配置我们 —— 这是"USB Bulk 链路可承载数据"的唯一可靠判据。
 *
 * 为什么不用 /sys/class/udc/<udc>/state: dwc2 在 dr_mode=peripheral 下几乎不更新该字段。
 * 2026-09-15 板测: 主机每次在 gadget 绑定后都会发出 USBRst + EnumDone 完成高速握手, 但 state
 * 始终停在 "not attached"; 用它作门禁会让 Bulk 通道永远不注册。
 *
 * 可靠信号来自 ep0 事件流: 主机发 SET_CONFIGURATION 时, 内核把 FUNCTIONFS_ENABLE 事件写进
 * 各 ffs 实例的 ep0 读队列。事件是 8 字节二进制且 type 字段带 NUL, shell 无法解析, 因此由随包
 * 发布的 usb_bulk_event_watcher 独占读取, 结果落到状态文件 (USB_BULK_HOST_STATE_FILE)。 */
static bool DjiUser_IsUsbBulkHostConfigured()
{
    ::std::ifstream stateStream { USB_BULK_HOST_STATE_FILE };
    if (!stateStream)
    {
        return false; // 监听程序未运行 ⇒ 状态未知 ⇒ 按"未配置"处理 (退化为仅 UART)
    }

    ::std::string line {};
    while (::std::getline(stateStream, line))
    {
        if (line == "host_configured=1")
        {
            return true;
        }
    }

    return false;
}

void Application::DjiUser_SetupEnvironment()
{
    ::T_DjiReturnCode        returnCode;
    ::T_DjiOsalHandler       osalHandler {};
    ::T_DjiHalUartHandler    uartHandler {};
    ::T_DjiHalUsbBulkHandler usbBulkHandler {};
    ::T_DjiLoggerConsole     printConsole;
    ::T_DjiLoggerConsole     localRecordConsole;
    ::T_DjiFileSystemHandler fileSystemHandler {};
    ::T_DjiSocketHandler     socketHandler {};
    ::T_DjiHalNetworkHandler networkHandler {};
    ::T_DjiHalI2cHandler     i2CHandler {};

    networkHandler.NetworkInit          = ::HalNetWork_Init;
    networkHandler.NetworkDeInit        = ::HalNetWork_DeInit;
    networkHandler.NetworkGetDeviceInfo = ::HalNetWork_GetDeviceInfo;

    socketHandler.Socket                = ::Osal_Socket;
    socketHandler.Bind                  = ::Osal_Bind;
    socketHandler.Close                 = ::Osal_Close;
    socketHandler.UdpSendData           = ::Osal_UdpSendData;
    socketHandler.UdpRecvData           = ::Osal_UdpRecvData;
    socketHandler.TcpListen             = ::Osal_TcpListen;
    socketHandler.TcpAccept             = ::Osal_TcpAccept;
    socketHandler.TcpConnect            = ::Osal_TcpConnect;
    socketHandler.TcpSendData           = ::Osal_TcpSendData;
    socketHandler.TcpRecvData           = ::Osal_TcpRecvData;

    osalHandler.TaskCreate              = ::Osal_TaskCreate;
    osalHandler.TaskDestroy             = ::Osal_TaskDestroy;
    osalHandler.TaskSleepMs             = ::Osal_TaskSleepMs;
    osalHandler.MutexCreate             = ::Osal_MutexCreate;
    osalHandler.MutexDestroy            = ::Osal_MutexDestroy;
    osalHandler.MutexLock               = ::Osal_MutexLock;
    osalHandler.MutexUnlock             = ::Osal_MutexUnlock;
    osalHandler.SemaphoreCreate         = ::Osal_SemaphoreCreate;
    osalHandler.SemaphoreDestroy        = ::Osal_SemaphoreDestroy;
    osalHandler.SemaphoreWait           = ::Osal_SemaphoreWait;
    osalHandler.SemaphoreTimedWait      = ::Osal_SemaphoreTimedWait;
    osalHandler.SemaphorePost           = ::Osal_SemaphorePost;
    osalHandler.Malloc                  = ::Osal_Malloc;
    osalHandler.Free                    = ::Osal_Free;
    osalHandler.GetTimeMs               = ::Osal_GetTimeMs;
    osalHandler.GetTimeUs               = ::Osal_GetTimeUs;
    osalHandler.GetRandomNum            = ::Osal_GetRandomNum;

    printConsole.func                   = DjiUser_PrintConsole;
    printConsole.consoleLevel           = ::DJI_LOGGER_CONSOLE_LOG_LEVEL_INFO;
    printConsole.isSupportColor         = true;

    localRecordConsole.consoleLevel     = ::DJI_LOGGER_CONSOLE_LOG_LEVEL_DEBUG;
    localRecordConsole.func             = DjiUser_LocalWrite;
    localRecordConsole.isSupportColor   = false;

    uartHandler.UartInit                = ::HalUart_Init;
    uartHandler.UartDeInit              = ::HalUart_DeInit;
    uartHandler.UartWriteData           = ::HalUart_WriteData;
    uartHandler.UartReadData            = ::HalUart_ReadData;
    uartHandler.UartGetStatus           = ::HalUart_GetStatus;
    uartHandler.UartGetDeviceInfo       = ::HalUart_GetDeviceInfo;
    i2CHandler.I2cInit                  = ::HalI2c_Init;
    i2CHandler.I2cDeInit                = ::HalI2c_DeInit;
    i2CHandler.I2cWriteData             = ::HalI2c_WriteData;
    i2CHandler.I2cReadData              = ::HalI2c_ReadData;

    usbBulkHandler.UsbBulkInit          = ::HalUsbBulk_Init;
    usbBulkHandler.UsbBulkDeInit        = ::HalUsbBulk_DeInit;
    usbBulkHandler.UsbBulkWriteData     = ::HalUsbBulk_WriteData;
    usbBulkHandler.UsbBulkReadData      = ::HalUsbBulk_ReadData;
    usbBulkHandler.UsbBulkGetDeviceInfo = ::HalUsbBulk_GetDeviceInfo;

    fileSystemHandler.FileOpen = ::Osal_FileOpen, fileSystemHandler.FileClose = ::Osal_FileClose, fileSystemHandler.FileWrite = ::Osal_FileWrite,
    fileSystemHandler.FileRead = ::Osal_FileRead, fileSystemHandler.FileSync = ::Osal_FileSync, fileSystemHandler.FileSeek = ::Osal_FileSeek,
    fileSystemHandler.DirOpen = ::Osal_DirOpen, fileSystemHandler.DirClose = ::Osal_DirClose, fileSystemHandler.DirRead = ::Osal_DirRead,
    fileSystemHandler.Mkdir = ::Osal_Mkdir, fileSystemHandler.Unlink = ::Osal_Unlink, fileSystemHandler.Rename = ::Osal_Rename,
    fileSystemHandler.Stat = ::Osal_Stat, returnCode = ::DjiPlatform_RegOsalHandler(&osalHandler);
    if (returnCode != ::DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
    {
        throw ::std::runtime_error("Register osal handler error.");
    }

    // 注册 I2C HAL (Raspberry Pi 平台支持)
    returnCode = ::DjiPlatform_RegHalI2cHandler(&i2CHandler);
    if (returnCode != ::DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
    {
        throw ::std::runtime_error("Register hal i2c handler error.");
    }

// 按硬件连接方式注册 HAL (与 raspberry_pi 官方样例对齐)
#if (CONFIG_HARDWARE_CONNECTION == DJI_USE_UART_AND_USB_BULK_DEVICE)
    returnCode = ::DjiPlatform_RegHalUartHandler(&uartHandler);
    if (returnCode != ::DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
    {
        throw ::std::runtime_error("Register hal uart handler error.");
    }

    // USB Bulk 承载视频/高带宽数据, 依赖树莓派 OTG gadget (/dev/usb-ffs/bulk{1,2,3})。
    // 注册策略由 features.usb_bulk 决定:
    //   auto (缺省): 仅当飞机已配置我们 (见 DjiUser_IsUsbBulkHostConfigured) 时注册 —— 飞机
    //                未配置时若注册该链路, PSDK 的 payload negotiate 会因该通道无对端而超时
    //                (225 TIMEOUT), 使整个 Core init 失败;
    //   force:      无条件注册 (现场验证用: 观察飞机是否要等 PSDK 请求才拉起 USB 主机);
    //   off:        不注册, 退化为仅 UART。
    const auto usbBulkPolicy { ::plane::config::ConfigManager::getInstance().getUsbBulkPolicy() };
    const bool usbBulkGadgetReady { ::access("/dev/usb-ffs/bulk1/ep1", F_OK) == 0 };
    bool       usbBulkRegister { false };

    switch (usbBulkPolicy)
    {
        case ::plane::config::ConfigManager::UsbBulkPolicy::Force:
            usbBulkRegister = usbBulkGadgetReady;
            break;
        case ::plane::config::ConfigManager::UsbBulkPolicy::Auto:
            usbBulkRegister = DjiUser_IsUsbBulkHostConfigured();
            break;
        case ::plane::config::ConfigManager::UsbBulkPolicy::Off:
            usbBulkRegister = false;
            break;
    }

    if (usbBulkRegister)
    {
        returnCode = ::DjiPlatform_RegHalUsbBulkHandler(&usbBulkHandler);
        if (returnCode != ::DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
        {
            throw ::std::runtime_error("Register hal usb bulk handler error.");
        }
        if (usbBulkPolicy == ::plane::config::ConfigManager::UsbBulkPolicy::Force)
        {
            LOG_WARN(
                "features.usb_bulk=force: 跳过'飞机已配置我们'检查直接注册; 若飞机未配置该通道, "
                "PSDK 会在 payload negotiate 阶段超时 (225) 导致 Core init 失败"
            );
        }
        LOG_INFO("USB Bulk 链路已启用 (视频/高带宽数据可用)");
    }
    else if (!usbBulkGadgetReady)
    {
        LOG_WARN("未检测到 USB Bulk gadget (/dev/usb-ffs/bulk1/ep1): 本次仅 UART 链路, 视频/高带宽数据不可用");
        LOG_WARN("可执行 sudo bash usb_bulk_config.sh --check 查看设备侧状态");
    }
    else if (usbBulkPolicy == ::plane::config::ConfigManager::UsbBulkPolicy::Off)
    {
        LOG_INFO("features.usb_bulk=off: 按配置不注册 USB Bulk, 本次仅 UART 链路");
    }
    else
    {
        LOG_WARN("USB Bulk gadget 已就绪, 但飞机未配置该链路 (状态文件 host_configured=0): 本次仅 UART 链路, 视频/高带宽数据不可用");
        LOG_WARN("核对: 飞机已上电启动 / E-Port 开发板 USB 主从拨码=Host / 同轴线 A-B 面 / 标识5 用 USB-A 转 USB-C 接树莓派 Type-C");
        LOG_WARN("也可设 features.usb_bulk=force 强制注册该链路做现场验证");
    }
#elif (CONFIG_HARDWARE_CONNECTION == DJI_USE_UART_AND_NETWORK_DEVICE)
    returnCode = ::DjiPlatform_RegHalUartHandler(&uartHandler);
    if (returnCode != ::DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
    {
        throw ::std::runtime_error("Register hal uart handler error.");
    }

    returnCode = ::DjiPlatform_RegHalNetworkHandler(&networkHandler);
    if (returnCode != ::DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
    {
        throw ::std::runtime_error("Register hal network handler error");
    }
#elif (CONFIG_HARDWARE_CONNECTION == DJI_USE_ONLY_USB_BULK_DEVICE)
    returnCode = ::DjiPlatform_RegHalUsbBulkHandler(&usbBulkHandler);
    if (returnCode != ::DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
    {
        throw ::std::runtime_error("Register hal usb bulk handler error.");
    }
#elif (CONFIG_HARDWARE_CONNECTION == DJI_USE_ONLY_NETWORK_DEVICE)
    returnCode = ::DjiPlatform_RegHalNetworkHandler(&networkHandler);
    if (returnCode != ::DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
    {
        throw ::std::runtime_error("Register hal network handler error");
    }

    // Attention: if you want to use camera stream view function, please uncomment it.
    returnCode = ::DjiPlatform_RegSocketHandler(&socketHandler);
    if (returnCode != ::DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
    {
        throw ::std::runtime_error("register osal socket handler error");
    }

#elif (CONFIG_HARDWARE_CONNECTION == DJI_USE_ONLY_UART)
    /*!< Attention: Only use uart hardware connection. */
    returnCode = ::DjiPlatform_RegHalUartHandler(&uartHandler);
    if (returnCode != ::DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
    {
        throw ::std::runtime_error("Register hal uart handler error.");
    }
#endif

    // Attention: if you want to use camera stream view function, please uncomment it.
    returnCode = ::DjiPlatform_RegSocketHandler(&socketHandler);
    if (returnCode != ::DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
    {
        throw ::std::runtime_error("register osal socket handler error");
    }

    returnCode = ::DjiPlatform_RegFileSystemHandler(&fileSystemHandler);
    if (returnCode != ::DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
    {
        throw ::std::runtime_error("Register osal filesystem handler error.");
    }

    // 将 PSDK 日志重定向到 spdlog (幂等; 平台注册完成之后, DjiCore_Init 之前, 与官方 AddConsole 位置一致)
    plane::manager::PSDKManager::getInstance().redirectPsdkLogs();

    // if (DjiUser_LocalWriteFsInit(DJI_LOG_PATH) != ::DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    //   throw ::std::runtime_error("File system init error.");
    // }

    // returnCode = ::DjiLogger_AddConsole(&printConsole);
    // if (returnCode != ::DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    //   throw ::std::runtime_error("Add printf console error.");
    // }

    // returnCode = ::DjiLogger_AddConsole(&localRecordConsole);
    // if (returnCode != ::DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    //   throw ::std::runtime_error("Add printf console error.");
    // }
}

void Application::DjiUser_ApplicationStart()
{
    ::T_DjiUserInfo             userInfo;
    ::T_DjiReturnCode           returnCode;
    ::T_DjiAircraftInfoBaseInfo aircraftInfoBaseInfo;
    ::T_DjiFirmwareVersion      firmwareVersion = {
        .majorVersion  = 1,
        .minorVersion  = 0,
        .modifyVersion = 0,
        .debugVersion  = 0,
    };

    // attention: when the program is hand up ctrl-c will generate the coredump file
    ::signal(SIGTERM, DjiUser_NormalExitHandler);

    returnCode = DjiUser_FillInUserInfo(&userInfo);
    if (returnCode != ::DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
    {
        throw ::std::runtime_error("Fill user info error, please check user info config.");
    }

    returnCode = ::DjiCore_SetFirmwareVersion(firmwareVersion);
    if (returnCode != ::DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
    {
        throw ::std::runtime_error("Set firmware version error.");
    }

    returnCode = ::DjiCore_SetSerialNumber(USER_PAYLOAD_SERIAL);
    if (returnCode != ::DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
    {
        throw ::std::runtime_error("Set serial number error");
    }

    returnCode = ::DjiCore_Init(&userInfo);
    if (returnCode != ::DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
    {
        ::sleep(1);
        throw ::std::runtime_error("Core init error.");
    }

    returnCode = ::DjiAircraftInfo_GetBaseInfo(&aircraftInfoBaseInfo);
    if (returnCode != ::DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
    {
        throw ::std::runtime_error("Get aircraft base info error.");
    }

    if (aircraftInfoBaseInfo.mountPosition != ::DJI_MOUNT_POSITION_EXTENSION_PORT &&
        aircraftInfoBaseInfo.djiAdapterType != ::DJI_SDK_ADAPTER_TYPE_EPORT_V2_RIBBON_CABLE &&
        aircraftInfoBaseInfo.djiAdapterType != ::DJI_SDK_ADAPTER_TYPE_SKYPORT_V3)
    {
        throw ::std::runtime_error("Please run this sample on extension port or skyport v3.");
    }

    returnCode = ::DjiCore_SetAlias("PSDK_APPALIAS");
    if (returnCode != ::DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
    {
        throw ::std::runtime_error("Set alias error.");
    }

#ifdef CONFIG_MODULE_SAMPLE_CAMERA_EMU_ON
    returnCode = ::DjiTest_CameraEmuBaseStartService();
    if (returnCode != ::DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
    {
        USER_LOG_ERROR("camera emu common init error");
    }
#endif

#ifdef CONFIG_MODULE_SAMPLE_CAMERA_MEDIA_ON
    returnCode = ::DjiTest_CameraEmuMediaStartService();
    if (returnCode != ::DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
    {
        USER_LOG_ERROR("camera emu media init error");
    }
#endif

#ifdef CONFIG_MODULE_SAMPLE_GIMBAL_EMU_ON
    returnCode = ::DjiTest_GimbalStartService();
    if (returnCode != ::DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
    {
        USER_LOG_ERROR("psdk gimbal init error");
    }
#endif

#ifdef CONFIG_MODULE_SAMPLE_WIDGET_ON
    returnCode = ::DjiTest_WidgetStartService();
    if (returnCode != ::DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
    {
        USER_LOG_ERROR("widget sample init error");
    }
#endif

#ifdef CONFIG_MODULE_SAMPLE_WIDGET_SPEAKER_ON
    returnCode = ::DjiTest_WidgetSpeakerStartService();
    if (returnCode != ::DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
    {
        USER_LOG_ERROR("widget speaker test init error");
    }
#endif

#ifdef CONFIG_MODULE_SAMPLE_POWER_MANAGEMENT_ON
    ::T_DjiTestApplyHighPowerHandler applyHighPowerHandler = {
        .pinInit  = DjiTest_HighPowerApplyPinInit,
        .pinWrite = DjiTest_WriteHighPowerApplyPin,
    };

    returnCode = ::DjiTest_RegApplyHighPowerHandler(&applyHighPowerHandler);
    if (returnCode != ::DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
    {
        USER_LOG_ERROR("regsiter apply high power handler error");
    }

    returnCode = ::DjiTest_PowerManagementStartService();
    if (returnCode != ::DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
    {
        USER_LOG_ERROR("power management init error");
    }
#endif

#ifdef CONFIG_MODULE_SAMPLE_DATA_TRANSMISSION_ON
    returnCode = ::DjiTest_DataTransmissionStartService();
    if (returnCode != ::DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
    {
        USER_LOG_ERROR("widget sample init error");
    }
#endif

    returnCode = ::DjiCore_ApplicationStart();
    if (returnCode != ::DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
    {
        throw ::std::runtime_error("Start sdk application error.");
    }

    USER_LOG_INFO("Application start.");
}

::T_DjiReturnCode Application::DjiUser_PrintConsole(const uint8_t* data, uint16_t)
{
    ::printf("%s", data);

    return ::DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

::T_DjiReturnCode Application::DjiUser_LocalWrite(const uint8_t* data, uint16_t dataLen)
{
    int32_t realLen;

    if (s_djiLogFile == nullptr)
    {
        return ::DJI_ERROR_SYSTEM_MODULE_CODE_UNKNOWN;
    }

    realLen = static_cast<int32_t>(::fwrite(data, 1, dataLen, s_djiLogFile));
    ::fflush(s_djiLogFile);
    if (realLen == dataLen)
    {
        return ::DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
    }
    else
    {
        return ::DJI_ERROR_SYSTEM_MODULE_CODE_UNKNOWN;
    }
}

::T_DjiReturnCode Application::DjiUser_FillInUserInfo(::T_DjiUserInfo* userInfo)
{
    ::memset(userInfo->appName, 0, sizeof(userInfo->appName));
    ::memset(userInfo->appId, 0, sizeof(userInfo->appId));
    ::memset(userInfo->appKey, 0, sizeof(userInfo->appKey));
    ::memset(userInfo->appLicense, 0, sizeof(userInfo->appLicense));
    ::memset(userInfo->developerAccount, 0, sizeof(userInfo->developerAccount));
    ::memset(userInfo->baudRate, 0, sizeof(userInfo->baudRate));

    if (::strlen(USER_APP_NAME) >= sizeof(userInfo->appName) || ::strlen(USER_APP_ID) > sizeof(userInfo->appId) ||
        ::strlen(USER_APP_KEY) > sizeof(userInfo->appKey) || ::strlen(USER_APP_LICENSE) > sizeof(userInfo->appLicense) ||
        ::strlen(USER_DEVELOPER_ACCOUNT) >= sizeof(userInfo->developerAccount) || ::strlen(USER_BAUD_RATE) > sizeof(userInfo->baudRate))
    {
        USER_LOG_ERROR("Length of user information string is beyond limit. Please check.");
        return ::DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
    }

    if (!::strcmp(USER_APP_NAME, "your_app_name") || !::strcmp(USER_APP_ID, "your_app_id") || !::strcmp(USER_APP_KEY, "your_app_key") ||
        !::strcmp(USER_BAUD_RATE, "your_app_license") || !::strcmp(USER_DEVELOPER_ACCOUNT, "your_developer_account") ||
        !::strcmp(USER_BAUD_RATE, "your_baud_rate"))
    {
        USER_LOG_ERROR(
            "Please fill in correct user information to 'samples/sample_c++/platform/linux/cy_psdk/src/application/dji_sdk_app_info.h' file."
        );
        return ::DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
    }

    ::strncpy(userInfo->appName, USER_APP_NAME, sizeof(userInfo->appName) - 1);
    ::memcpy(userInfo->appId, USER_APP_ID, USER_UTIL_MIN(sizeof(userInfo->appId), ::strlen(USER_APP_ID)));
    ::memcpy(userInfo->appKey, USER_APP_KEY, USER_UTIL_MIN(sizeof(userInfo->appKey), ::strlen(USER_APP_KEY)));
    ::memcpy(userInfo->appLicense, USER_APP_LICENSE, USER_UTIL_MIN(sizeof(userInfo->appLicense), ::strlen(USER_APP_LICENSE)));
    ::memcpy(userInfo->baudRate, USER_BAUD_RATE, USER_UTIL_MIN(sizeof(userInfo->baudRate), ::strlen(USER_BAUD_RATE)));
    ::strncpy(userInfo->developerAccount, USER_DEVELOPER_ACCOUNT, sizeof(userInfo->developerAccount) - 1);

    return ::DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

::T_DjiReturnCode Application::DjiUser_LocalWriteFsInit(const char* path)
{
    ::T_DjiReturnCode djiReturnCode = ::DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
    char              filePath[DJI_LOG_PATH_MAX_SIZE];
    char              systemCmd[DJI_SYSTEM_CMD_STR_MAX_SIZE];
    char              folderName[DJI_LOG_FOLDER_NAME_MAX_SIZE];
    ::time_t          currentTime  = ::time(nullptr);
    struct tm*        localTime    = ::localtime(&currentTime);
    uint16_t          logFileIndex = 0;
    uint16_t          currentLogFileIndex;
    int32_t           ret;

    if (localTime == nullptr)
    {
        ::printf("Get local time error.\r\n");
        return ::DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
    }

    if (::access(DJI_LOG_FOLDER_NAME, F_OK) != 0)
    {
        ::sprintf(folderName, "mkdir %s", DJI_LOG_FOLDER_NAME);
        ret = ::system(folderName);
        if (ret != 0)
        {
            ::printf("Create new log folder error, ret:%d.\r\n", ret);
            return ::DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
        }
    }

    s_djiLogFileCnt = ::fopen(DJI_LOG_INDEX_FILE_NAME, "rb+");
    if (s_djiLogFileCnt == nullptr)
    {
        s_djiLogFileCnt = ::fopen(DJI_LOG_INDEX_FILE_NAME, "wb+");
        if (s_djiLogFileCnt == nullptr)
        {
            return ::DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
        }
    }
    else
    {
        ret = ::fseek(s_djiLogFileCnt, 0, SEEK_SET);
        if (ret != 0)
        {
            ::printf("Seek log count file error, ret: %d, errno: %d.\r\n", ret, errno);
            return ::DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
        }

        ret = static_cast<int32_t>(::fread((uint16_t*)&logFileIndex, 1, sizeof(uint16_t), s_djiLogFileCnt));
        if (ret != sizeof(uint16_t))
        {
            ::printf("Read log file index error.\r\n");
        }
    }

    currentLogFileIndex = logFileIndex;
    logFileIndex++;

    ret = ::fseek(s_djiLogFileCnt, 0, SEEK_SET);
    if (ret != 0)
    {
        ::printf("Seek log file error, ret: %d, errno: %d.\r\n", ret, errno);
        return ::DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
    }

    ret = static_cast<int32_t>(::fwrite((uint16_t*)&logFileIndex, 1, sizeof(uint16_t), s_djiLogFileCnt));
    if (ret != sizeof(uint16_t))
    {
        ::printf("Write log file index error.\r\n");
        ::fclose(s_djiLogFileCnt);
        return ::DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
    }

    ::fclose(s_djiLogFileCnt);

    ::sprintf(
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

    s_djiLogFile = ::fopen(filePath, "wb+");
    if (s_djiLogFile == nullptr)
    {
        USER_LOG_ERROR("Open filepath time error.");
        return ::DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
    }

    if (logFileIndex >= DJI_LOG_MAX_COUNT)
    {
        ::sprintf(systemCmd, "rm -rf %s_%04d*.log", path, currentLogFileIndex - DJI_LOG_MAX_COUNT);
        ret = ::system(systemCmd);
        if (ret != 0)
        {
            ::printf("Remove file error, ret:%d.\r\n", ret);
            return ::DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
        }
    }

    ::sprintf(systemCmd, "ln -sfrv %s " DJI_LOG_FOLDER_NAME "/latest.log", filePath);
    int rc = ::system(systemCmd);
    if (rc != 0)
    {
        return ::DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
    }
    return djiReturnCode;
}

static void DjiUser_NormalExitHandler(int signalNum)
{
    USER_UTIL_UNUSED(signalNum);
    ::exit(0);
}

static ::T_DjiReturnCode DjiTest_HighPowerApplyPinInit()
{
    return ::DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

static ::T_DjiReturnCode DjiTest_WriteHighPowerApplyPin(::E_DjiPowerManagementPinState)
{
    // attention: please pull up the HWPR pin state by hardware.
    return ::DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

/****************** (C) COPYRIGHT DJI Innovations *****END OF FILE****/

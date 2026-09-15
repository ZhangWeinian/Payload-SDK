/**
 ********************************************************************
 * @file    dji_sdk_config.h
 * @brief   This is the header file for "dji_config.c", defining the structure and
 * (exported) function prototypes.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef DJI_SDK_CONFIG_H
#define DJI_SDK_CONFIG_H

/* Includes ------------------------------------------------------------------*/

#ifdef __cplusplus
extern "C"
{
#endif

/* Exported constants --------------------------------------------------------*/
#define DJI_USE_ONLY_UART                (0)
#define DJI_USE_UART_AND_USB_BULK_DEVICE (1)
#define DJI_USE_UART_AND_NETWORK_DEVICE  (2)
#define DJI_USE_ONLY_USB_BULK_DEVICE     (3)
#define DJI_USE_ONLY_NETWORK_DEVICE      (4)

/*!< Attention: Select your hardware connection mode here.
 * UART + USB Bulk:
 *   - UART:     命令/遥测 (当前板端接线, 必需);
 *   - USB Bulk: 视频与高带宽数据 (需树莓派 OTG gadget 就绪; 且取流/运动规划等需高级许可)。
 *     application.cpp 注册前会探测 /dev/usb-ffs/bulk1/ep1, gadget 未就绪时自动只走 UART, 不影响启动。
 * */
#define CONFIG_HARDWARE_CONNECTION DJI_USE_UART_AND_USB_BULK_DEVICE

    /*!< Attention: Select the sample you want to run here.
     * 生产配置: 关闭会与本机真实角色冲突或本项目未使用的官方示例服务:
     *   - GIMBAL_EMU: 把本机伪装成三方云台负载, 与控制飞机云台 (GimbalManager) 冲突;
     *   - WIDGET / WIDGET_SPEAKER: 遥控器 UI/语音示例, 本项目未使用;
     *   - POWER_MANAGEMENT / DATA_TRANSMISSION: 保留 (供电申请与数据透传通道, 无冲突)。
     * 如需官方示例能力, 取消对应注释即可。
     * */
    // #define CONFIG_MODULE_SAMPLE_GIMBAL_EMU_ON

    // #define CONFIG_MODULE_SAMPLE_WIDGET_ON

    // #define CONFIG_MODULE_SAMPLE_WIDGET_SPEAKER_ON

#define CONFIG_MODULE_SAMPLE_POWER_MANAGEMENT_ON

#define CONFIG_MODULE_SAMPLE_DATA_TRANSMISSION_ON

    /* Exported types ------------------------------------------------------------*/

    /* Exported functions --------------------------------------------------------*/

#ifdef __cplusplus
}
#endif

#endif // DJI_SDK_CONFIG_H
/************************ (C) COPYRIGHT DJI Innovations *******END OF FILE******/

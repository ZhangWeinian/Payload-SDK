#ifndef DJI_SDK_CONFIG_H
#define DJI_SDK_CONFIG_H

#ifdef __cplusplus
extern "C"
{
#endif

#define DJI_USE_ONLY_UART				 (0)
#define DJI_USE_UART_AND_USB_BULK_DEVICE (1)
#define DJI_USE_UART_AND_NETWORK_DEVICE	 (2)

/*!< Attention: Select your hardware connection mode here.
 * */
#define CONFIG_HARDWARE_CONNECTION DJI_USE_ONLY_UART

/*!< Attention: Select the sample you want to run here.
 * */
#define CONFIG_MODULE_SAMPLE_GIMBAL_EMU_ON

#define CONFIG_MODULE_SAMPLE_WIDGET_ON

#define CONFIG_MODULE_SAMPLE_WIDGET_SPEAKER_ON

#define CONFIG_MODULE_SAMPLE_POWER_MANAGEMENT_ON

#define CONFIG_MODULE_SAMPLE_DATA_TRANSMISSION_ON

#ifdef __cplusplus
}
#endif

#endif // DJI_SDK_CONFIG_H

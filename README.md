# DJI 负载 SDK（PSDK）

![](https://img.shields.io/badge/version-V3.13.1-purple.svg)
![](https://img.shields.io/badge/platform-linux_|_rtos-red.svg)
![](https://img.shields.io/badge/license-MIT-cyan.svg)

## 什么是 DJI 负载 SDK？

DJI 负载 SDK（Payload SDK，简称 PSDK）是大疆（DJI）为开发者提供的一套开发工具包，用于开发可挂载在 DJI 无人机上的负载设备。通过结合 X-Port 、SkyPort 或扩展端口适配器，开发者可以从无人机获取信息或其他资源。开发者可根据自身设计的软件逻辑与算法框架，开发出适用于 DJI 无人机的负载设备，以执行所需的功能，例如自动飞行控制器、负载控制器、视频图像分析平台、测绘相机、扩音器和探照灯等。

## 文档

完整文档请访问 [DJI 开发者文档](https://developer.dji.com/doc/payload-sdk-tutorial/en/)。有关代码的详细说明，请参阅开发者网站中的 [PSDK API 参考文档](https://developer.dji.com/doc/payload-sdk-api-reference/en/)。如需获取最新版本信息，请访问 [最新版本信息页面](https://developer.dji.com/doc/payload-sdk-tutorial/en/)。

## 最新版本

PSDK 的最新发布版本为 3.13.1 。此版本主要新增了一些功能支持，并修复了若干问题。详细变更列表请参阅发布说明。

### 已发布功能列表

* 支持 Mavic 3TA 机型

### 问题修复与性能改进

* 修复了在 Matrice 300 上调用 `DjiCore_Init` API 失败的问题。  
* 修复了 Matrice 350 RTK 无法成功订阅四元数数据的问题。  
* 修复了 `DjiCore_Deinit` API 偶尔失败的问题。  
* 修复了自定义 HMS 模块偶尔导致程序崩溃的问题。  
* 默认不再支持无遥控器飞行（RC-less flight），并新增了 `DjiFlightController_SetRCLostActionEnableStatus` API，用于启用或禁用遥控器信号丢失时的响应动作。  
> **注意**：如需使用无遥控器飞行功能，必须在调用 `DjiFlightController_Init` 后调用此接口以禁用遥控器丢失动作。具体使用方法请参见接口头文件中的文档说明。

## 许可证

Payload SDK 代码库采用 MIT 许可证。详细信息请参阅 LICENSE 文件。

## 支持

您可以通过以下方式获得来自 DJI 官方及社区的支持：

- 在开发者论坛中提问  
  * [DJI SDK 开发者论坛（中文）](https://djisdksupport.zendesk.com/hc/zh-cn/community/topics)  
  * [DJI SDK 开发者论坛（英文）](https://djisdksupport.zendesk.com/hc/en-us/community/topics)  
- 在开发者支持页面提交问题描述  
  * [DJI SDK 开发者支持（中文）](https://djisdksupport.zendesk.com/hc/zh-cn/requests/new)  
  * [DJI SDK 开发者支持（英文）](https://djisdksupport.zendesk.com/hc/en-us/requests/new)

您也可以通过以下方式与其他开发者交流：

- 在 [**Stack Overflow**](http://stackoverflow.com) 上使用 [**dji-sdk**](http://stackoverflow.com/questions/tagged/dji-sdk) 标签提问

## 关于 Pull Request

DJI 开发团队始终致力于提升您的开发体验，我们也欢迎您的贡献。但请注意，我们可能无法及时审核所有的 Pull Request。如有任何疑问，请发送邮件至 dev@dji.com 。

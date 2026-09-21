// cy_psdk/utils/device_identity/DeviceIdentity.h

#pragma once

#include <string>

#include "define.h"

namespace plane::utils
{
    // 设备标识解析 (全局唯一身份, 绕不伪造):
    //   1) 显式配置 plane.code —— 部署方填写的本机 SN, 优先级最高 (填了就以它为准, 不再看飞控);
    //   2) 未配置时用飞控真实序列号 (PSDKAdapter 周期性重试读取, 覆盖飞机断电重启后重新接入);
    //   3) 均未就绪时返回空串 (调用方等待并重试, 不产生任何占位/兵底值 —— 身份错一次会长期错下去)
    // 注意: 已彻底移除 SDK CC 序列号兵底 (认证芯片序列号不是飞机身份)
    struct DeviceIdentity
    {
        [[nodiscard]] static ::std::string resolveDeviceCode(void) noexcept;

        // 目录注册 service_id: "swarm.agent.<code>"; code 未就绪时返回空串
        [[nodiscard]] static ::std::string resolveCatalogServiceId(void) noexcept;

        // 目录注册 service_name: "DJI-PSDK-<code>"; code 未就绪时返回空串
        [[nodiscard]] static ::std::string resolveCatalogServiceName(void) noexcept;
    };
} // namespace plane::utils

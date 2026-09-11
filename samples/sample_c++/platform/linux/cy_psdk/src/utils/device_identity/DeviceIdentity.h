// cy_psdk/utils/device_identity/DeviceIdentity.h

#pragma once

#include <string>

#include "define.h"

namespace plane::utils
{
	// 设备标识解析 (真实来源优先, 绝不伪造):
	//   1) 显式配置 plane.code (部署方填写);
	//   2) PSDK 真实序列号 (飞控就绪后由 PSDKAdapter 写入域模型);
	//   3) 均未就绪时返回空串 (调用方等待, 不产生任何占位值)
	struct DeviceIdentity
	{
		_NODISCARD static _STD string resolveDeviceCode(void) noexcept;

		// 目录注册 service_id: "swarm.agent.<code>"; code 未就绪时返回空串
		_NODISCARD static _STD string resolveCatalogServiceId(void) noexcept;

		// 目录注册 service_name: "DJI-PSDK-<code>"; code 未就绪时返回空串
		_NODISCARD static _STD string resolveCatalogServiceName(void) noexcept;
	};
} // namespace plane::utils

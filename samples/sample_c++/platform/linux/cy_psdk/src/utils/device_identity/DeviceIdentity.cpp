// cy_psdk/utils/device_identity/DeviceIdentity.cpp

#include "utils/device_identity/DeviceIdentity.h"

#include <fmt/format.h>

#include "config/ConfigManager.h"
#include "manager/plane_state/PlaneStateStore.h"

namespace plane::utils
{
	_STD string DeviceIdentity::resolveDeviceCode(void) noexcept
	{
		// 警告: 本函数可能读取 PlaneStateStore 快照 (plane.code 为空时);
		// 严禁在 PlaneStateStore::update() 的回调内调用 (std::mutex 不可重入, 会自锁)
		// 1) 显式配置优先 (部署方填写的真实设备标识)
		if (const auto& configured { plane::config::ConfigManager::getInstance().getPlaneCode() }; !configured.empty())
		{
			return _STD string { configured };
		}

		// 2) PSDK 真实序列号 (飞控就绪后写入域模型; 未取得时为空串)
		return plane::domain::PlaneStateStore::getInstance().snapshot().serial_number;
	}

	_STD string DeviceIdentity::resolveCatalogServiceId(void) noexcept
	{
		if (const auto code { resolveDeviceCode() }; !code.empty())
		{
			return _FMT format("swarm.agent.{}", code);
		}
		return {};
	}

	_STD string DeviceIdentity::resolveCatalogServiceName(void) noexcept
	{
		if (const auto code { resolveDeviceCode() }; !code.empty())
		{
			return _FMT format("DJI-PSDK-{}", code);
		}
		return {};
	}
} // namespace plane::utils

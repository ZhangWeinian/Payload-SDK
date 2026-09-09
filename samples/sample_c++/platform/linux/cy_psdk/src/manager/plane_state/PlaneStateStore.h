// cy_psdk/manager/plane_state/PlaneStateStore.h
//
// 内部"最新状态"运行时存储 (线程安全单例)。
// 对应 msdk 的 core/state/PlaneState.kt (状态持有者, 不属于 domain 纯数据镜像层);
// 持有单一数据类 domain/PlaneStateDataClass.h (与 msdk 1:1)。
//   - PSDKAdapter 采集循环 / CatalogManager / MQTTv5Service / 启动链 -> update(...)
//   - TelemetryReporter / 其它消费方 -> snapshot() 拷贝读取

#pragma once

#include "domain/PlaneStateDataClass.h"

#include <functional>
#include <mutex>

namespace plane::domain
{
	class PlaneStateStore
	{
	public:
		static PlaneStateStore& getInstance(void) noexcept;

		// 整份替换最新状态
		void update(PlaneStateDataClass data) noexcept;

		// 就地更新最新状态 (回调在锁内执行, 不得阻塞/重入)
		void update(const _STD function<void(PlaneStateDataClass&)>& mutator) noexcept;

		// 取快照 (线程安全, 返回拷贝)
		_NODISCARD PlaneStateDataClass snapshot(void) const noexcept;

	private:
		PlaneStateStore(void) noexcept						  = default;
		~PlaneStateStore(void) noexcept						  = default;
		PlaneStateStore(const PlaneStateStore&)				  = delete;
		PlaneStateStore&	operator=(const PlaneStateStore&) = delete;

		mutable _STD mutex	mutex_ {};
		PlaneStateDataClass state_ {};
	};
} // namespace plane::domain

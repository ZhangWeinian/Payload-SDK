// cy_psdk/manager/binding/DeviceBinder.h
//
// 设备绑定 (对齐 msdk NicknameSync):
//   - 前提: 目录就绪 (CatalogManager Ready) + 本机序列号就绪 (不兜底, 无 SN 不发起)
//   - 经 Catalog 定位后台"业务"服务 (swarm.service.base, http)
//   - POST {base}/control/bindDeviceBySerialNumber
//       body: { planeTypeCode, serialNumber, imageUrl }
//   - 成功: 回写 device_nickname / internal_plane_id / device_binding, 并昵称联动目录注册名
//   - 触发: 首次 / 目录失联后恢复 / 序列号变化; 失败按固定周期重试

#pragma once

#include <atomic>
#include <thread>

#include "define.h"

namespace plane::manager
{
	class DeviceBinder
	{
	public:
		static DeviceBinder& getInstance(void) noexcept;

		// 后台启动绑定流程 (目录/SN 未就绪时自动等待, 不阻塞调用方)
		void start(void) noexcept;

		// 停止并 join 后台线程
		void stop(void) noexcept;

		// 是否已绑定成功 (只读标志)
		_NODISCARD bool isBound(void) const noexcept
		{
			return this->bound_.load(_STD memory_order_acquire);
		}

	private:
		DeviceBinder(void) noexcept = default;
		~DeviceBinder(void) noexcept;
		DeviceBinder(const DeviceBinder&)			 = delete;
		DeviceBinder& operator=(const DeviceBinder&) = delete;

		void		  runLoop(void) noexcept;

		// 后台线程生命周期 (防止 start()/stop() 重入)
		_STD atomic<bool> started_ { false };
		_STD atomic<bool> running_ { false };
		_STD atomic<bool> bound_ { false };
		_STD thread		  thread_ {};
	};
} // namespace plane::manager

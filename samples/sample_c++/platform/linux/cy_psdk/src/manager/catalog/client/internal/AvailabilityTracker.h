// cy_psdk/manager/catalog/client/internal/AvailabilityTracker.h
//
// 连续失败计数, 达到阈值判定"可重试失败已熔断" (对齐 java AvailabilityTracker)。

#pragma once

#include <atomic>

#include "define.h"

namespace plane::catalog::internal
{
	class AvailabilityTracker
	{
	public:
		explicit AvailabilityTracker(int threshold) noexcept: threshold_(threshold < 1 ? 1 : threshold) {}

		// 计数一次可重试失败; 达到阈值返回 true
		bool retryableFailureTrips(void) noexcept
		{
			const int failures { this->failures_.fetch_add(1, _STD memory_order_acq_rel) + 1 };
			return failures >= this->threshold_;
		}

		void success(void) noexcept
		{
			this->failures_.store(0, _STD memory_order_release);
		}

		void reset(void) noexcept
		{
			this->success();
		}

	private:
		const int threshold_ { 3 };
		_STD atomic<int> failures_ { 0 };
	};
} // namespace plane::catalog::internal

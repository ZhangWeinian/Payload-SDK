// cy_psdk/manager/catalog/client/ConfigSubscription.h
//
// 可重复取消的配置监听句柄 (对齐 java ConfigSubscription)。
// 线程安全: cancel() 与析构可从业务线程调用。析构时自动取消。

#pragma once

#include <functional>
#include <utility>

#include "define.h"

namespace plane::catalog
{
	class ConfigSubscription
	{
	public:
		ConfigSubscription(void) noexcept = default;

		explicit ConfigSubscription(_STD function<void()> cancel): cancel_(_STD move(cancel)) {}

		~ConfigSubscription(void)
		{
			this->cancel();
		}

		ConfigSubscription(ConfigSubscription&& other) noexcept: cancel_(_STD move(other.cancel_)) {}

		ConfigSubscription& operator=(ConfigSubscription&& other) noexcept
		{
			if (this != &other)
			{
				this->cancel();
				this->cancel_ = _STD move(other.cancel_);
			}
			return *this;
		}

		ConfigSubscription(const ConfigSubscription&)			 = delete;
		ConfigSubscription& operator=(const ConfigSubscription&) = delete;

		// 取消监听。重复调用是安全的。
		void cancel(void)
		{
			if (!this->cancel_)
			{
				return;
			}
			_STD function<void()> action {};
			action.swap(this->cancel_);
			action();
		}

		_NODISCARD bool valid(void) const noexcept
		{
			return static_cast<bool>(this->cancel_);
		}

	private:
		_STD function<void()> cancel_ {};
	};
} // namespace plane::catalog

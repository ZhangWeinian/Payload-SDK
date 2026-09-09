// cy_psdk/manager/catalog/client/internal/RuntimeStateMachine.h
//
// 生命周期状态机 (对齐 java RuntimeStateMachine)。线程安全。

#pragma once

#include <atomic>

#include "define.h"
#include "manager/catalog/client/CatalogTypes.h"

namespace plane::catalog::internal
{
	class RuntimeStateMachine
	{
	public:
		RuntimeStateMachine(void) noexcept = default;

		_NODISCARD CatalogState state(void) const noexcept
		{
			return this->state_.load(_STD memory_order_acquire);
		}

		void set(CatalogState value) noexcept
		{
			this->state_.store(value, _STD memory_order_release);
		}

		// Discovered/Registering/Ready 允许查询
		_NODISCARD bool allowQuery(void) const noexcept
		{
			const CatalogState value { this->state() };
			return value == CatalogState::DISCOVERED || value == CatalogState::REGISTERING || value == CatalogState::READY;
		}

		_NODISCARD bool allowStatusReport(void) const noexcept
		{
			return this->state() == CatalogState::READY;
		}

		_NODISCARD bool allowRegister(void) const noexcept
		{
			return this->state() == CatalogState::REGISTERING;
		}

		_NODISCARD bool allowConfigRefresh(void) const noexcept
		{
			return this->allowQuery();
		}

		_NODISCARD bool returnCachedConfig(void) const noexcept
		{
			const CatalogState value { this->state() };
			return value == CatalogState::UNAVAILABLE || value == CatalogState::CONFLICT || value == CatalogState::REGISTERING;
		}

	private:
		_STD atomic<CatalogState> state_ { CatalogState::STOPPED };
	};
} // namespace plane::catalog::internal

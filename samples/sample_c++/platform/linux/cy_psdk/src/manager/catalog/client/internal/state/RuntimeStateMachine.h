// cy_psdk/manager/catalog/client/internal/state/RuntimeStateMachine.h
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

        [[nodiscard]] CatalogState state(void) const noexcept
        {
            return this->state_.load(::std::memory_order_acquire);
        }

        void set(CatalogState value) noexcept
        {
            this->state_.store(value, ::std::memory_order_release);
        }

        // Discovered/Registering/Ready 允许查询
        [[nodiscard]] bool allowQuery(void) const noexcept
        {
            const CatalogState value { this->state() };
            return value == CatalogState::DISCOVERED || value == CatalogState::REGISTERING || value == CatalogState::READY;
        }

        [[nodiscard]] bool allowStatusReport(void) const noexcept
        {
            return this->state() == CatalogState::READY;
        }

        [[nodiscard]] bool allowRegister(void) const noexcept
        {
            return this->state() == CatalogState::REGISTERING;
        }

        [[nodiscard]] bool allowConfigRefresh(void) const noexcept
        {
            return this->allowQuery();
        }

    private:
        ::std::atomic<CatalogState> state_ { CatalogState::STOPPED };
    };
} // namespace plane::catalog::internal

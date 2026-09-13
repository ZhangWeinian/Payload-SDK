// cy_psdk/manager/plane_state/PlaneStateStore.cpp

#include "manager/plane_state/PlaneStateStore.h"

namespace plane::domain
{
    // 进程级唯一实例: 函数静态变量 (C++11 起保证首次构造的线程安全), 生命周期与进程相同
    PlaneStateStore& PlaneStateStore::getInstance(void) noexcept
    {
        static PlaneStateStore instance {};
        return instance;
    }

    void PlaneStateStore::update(PlaneStateDataClass data) noexcept
    {
        ::std::lock_guard<::std::mutex> lock { this->mutex_ };
        this->state_ = ::std::move(data);
    }

    void PlaneStateStore::update(const ::std::function<void(PlaneStateDataClass&)>& mutator) noexcept
    {
        if (!mutator)
        {
            return;
        }
        ::std::lock_guard<::std::mutex> lock { this->mutex_ };
        mutator(this->state_);
    }

    PlaneStateDataClass PlaneStateStore::snapshot(void) const noexcept
    {
        ::std::lock_guard<::std::mutex> lock { this->mutex_ };
        return this->state_;
    }
} // namespace plane::domain

// cy_psdk/manager/plane_state/PlaneStateStore.cpp

#include "manager/plane_state/PlaneStateStore.h"

namespace plane::domain
{
	PlaneStateStore& PlaneStateStore::getInstance(void) noexcept
	{
		static PlaneStateStore instance {};
		return instance;
	}

	void PlaneStateStore::update(PlaneStateDataClass data) noexcept
	{
		_STD lock_guard<_STD mutex> lock { this->mutex_ };
		this->state_ = _STD			move(data);
	}

	void PlaneStateStore::update(const _STD function<void(PlaneStateDataClass&)>& mutator) noexcept
	{
		if (!mutator)
		{
			return;
		}
		_STD lock_guard<_STD mutex> lock { this->mutex_ };
		mutator(this->state_);
	}

	PlaneStateDataClass PlaneStateStore::snapshot(void) const noexcept
	{
		_STD lock_guard<_STD mutex> lock { this->mutex_ };
		return this->state_;
	}
} // namespace plane::domain

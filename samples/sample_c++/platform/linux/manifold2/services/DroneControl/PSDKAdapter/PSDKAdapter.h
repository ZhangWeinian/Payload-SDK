#pragma once

#include "dji_typedef.h"

#include "define.h"
#include "protocol/DroneDataClass.h"

#include <string_view>
#include <string>
#include <vector>

namespace plane::services
{
	class PSDKAdapter
	{
	public:
		static PSDKAdapter&		   getInstance(void) noexcept;

		_NODISCARD T_DjiReturnCode takeoff(_MAYBE_UNUSED const protocol::TakeoffPayload& takeoffParams) const noexcept;
		_NODISCARD T_DjiReturnCode goHome(void) const noexcept;
		_NODISCARD T_DjiReturnCode hover(void) const noexcept;
		_NODISCARD T_DjiReturnCode land(void) const noexcept;
		_NODISCARD T_DjiReturnCode waypointV3MissionStart(_STD string_view kmzFilePath) const noexcept;
		_NODISCARD T_DjiReturnCode setControlStrategy(int strategyCode) const noexcept;
		_NODISCARD T_DjiReturnCode flyCircleAroundPoint(const protocol::CircleFlyPayload& circleParams) const noexcept;

	private:
		explicit PSDKAdapter(void) noexcept					= default;
		~PSDKAdapter(void) noexcept							= default;
		PSDKAdapter(const PSDKAdapter&) noexcept			= delete;
		PSDKAdapter& operator=(const PSDKAdapter&) noexcept = delete;
	};
} // namespace plane::services

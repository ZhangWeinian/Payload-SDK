#pragma once

#include "dji_typedef.h"

#include "protocol/JsonProtocol.h"

#include <vector>

namespace plane::services
{
#ifndef _NODISCARD
	#define _NODISCARD [[nodiscard]]
#endif

	class PSDKAdapter
	{
	public:
		static PSDKAdapter&		   getInstance(void) noexcept;

		_NODISCARD T_DjiReturnCode takeoff(const protocol::TakeoffPayload& takeoffParams) const noexcept;
		_NODISCARD T_DjiReturnCode goHome(void) const noexcept;
		_NODISCARD T_DjiReturnCode hover(void) const noexcept;
		_NODISCARD T_DjiReturnCode land(void) const noexcept;
		_NODISCARD T_DjiReturnCode waypointV3MissionStart(const std::string& kmzFilePath) const noexcept;
		_NODISCARD T_DjiReturnCode setControlStrategy(int strategyCode) const noexcept;
		_NODISCARD T_DjiReturnCode flyCircleAroundPoint(const protocol::CircleFlyPayload& circleParams) const noexcept;

	private:
		PSDKAdapter(void) noexcept							= default;
		~PSDKAdapter(void) noexcept							= default;
		PSDKAdapter(const PSDKAdapter&) noexcept			= delete;
		PSDKAdapter& operator=(const PSDKAdapter&) noexcept = delete;
	};
} // namespace plane::services

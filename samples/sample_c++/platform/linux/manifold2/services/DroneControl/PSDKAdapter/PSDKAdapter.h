// manifold2/services/DroneControl/PSDKAdapter/PSDKAdapter.h

#pragma once

#include <string_view>
#include <string>
#include <vector>

#include <dji_fc_subscription.h>
#include <dji_typedef.h>

#include "protocol/DroneDataClass.h"
#include "protocol/HeartbeatDataClass.h"

#include "define.h"

namespace plane::services
{
	class PSDKAdapter
	{
	public:
		static PSDKAdapter& getInstance(void) noexcept;

		_NODISCARD bool		setup(void) noexcept;
		void				cleanup(void) noexcept;

		_NODISCARD plane::protocol::StatusPayload getLatestStatusPayload(void) noexcept;

		_NODISCARD _DJI T_DjiReturnCode			  takeoff(_MAYBE_UNUSED const plane::protocol::TakeoffPayload& takeoffParams) const noexcept;
		_NODISCARD _DJI T_DjiReturnCode			  goHome(void) const noexcept;
		_NODISCARD _DJI T_DjiReturnCode			  hover(void) const noexcept;
		_NODISCARD _DJI T_DjiReturnCode			  land(void) const noexcept;
		_NODISCARD _DJI T_DjiReturnCode			  waypointV3MissionStart(_STD string_view kmzFilePath) const noexcept;
		_NODISCARD _DJI T_DjiReturnCode			  setControlStrategy(int strategyCode) const noexcept;
		_NODISCARD _DJI T_DjiReturnCode			  flyCircleAroundPoint(const plane::protocol::CircleFlyPayload& circleParams) const noexcept;

	private:
		struct SubscriptionStatus
		{
			bool positionFused { false };
			bool altitudeFused { false };
			bool altitudeOfHomepoint { false };
			bool quaternion { false };
			bool velocity { false };
			bool batteryInfo { false };
			bool gimbalAngles { false };
		};

		SubscriptionStatus m_sub_status {};

		explicit PSDKAdapter(void) noexcept					= default;
		~PSDKAdapter(void) noexcept							= default;
		PSDKAdapter(const PSDKAdapter&) noexcept			= delete;
		PSDKAdapter& operator=(const PSDKAdapter&) noexcept = delete;

		void		 quaternionToEulerAngle(const _DJI T_DjiFcSubscriptionQuaternion& q, double& roll, double& pitch, double& yaw) noexcept;
	};
} // namespace plane::services

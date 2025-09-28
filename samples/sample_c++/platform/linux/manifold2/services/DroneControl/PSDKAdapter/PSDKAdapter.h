// manifold2/services/DroneControl/PSDKAdapter/PSDKAdapter.h

#pragma once

#include <string_view>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
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

		_NODISCARD bool		start(void) noexcept;
		void				stop(void) noexcept;

		_NODISCARD bool		setup(void) noexcept;
		void				cleanup(void) noexcept;

		_NODISCARD plane::protocol::StatusPayload getLatestStatusPayload(void) const noexcept;
		_NODISCARD _STD_CHRONO steady_clock::time_point getLastUpdateTime(void) const noexcept;

		_NODISCARD _DJI T_DjiReturnCode takeoff(_MAYBE_UNUSED const plane::protocol::TakeoffPayload& takeoffParams) const noexcept;
		_NODISCARD _DJI T_DjiReturnCode goHome(void) const noexcept;
		_NODISCARD _DJI T_DjiReturnCode hover(void) const noexcept;
		_NODISCARD _DJI T_DjiReturnCode land(void) const noexcept;
		_NODISCARD _DJI T_DjiReturnCode waypointV3MissionStart(_STD string_view kmzFilePath) const noexcept;
		_NODISCARD _DJI T_DjiReturnCode setControlStrategy(int strategyCode) const noexcept;
		_NODISCARD _DJI T_DjiReturnCode flyCircleAroundPoint(const plane::protocol::CircleFlyPayload& circleParams) const noexcept;

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
		_STD thread		   m_thread {};
		_STD atomic<bool>			   m_run { false };
		mutable _STD mutex			   m_payloadMutex {};
		plane::protocol::StatusPayload m_latestPayload {};
		mutable _STD mutex			   m_healthMutex {};
		_STD_CHRONO steady_clock::time_point m_lastUpdateTime {};
		constexpr static auto				 ACQUISITION_INTERVAL { _STD_CHRONO milliseconds(20) };

		explicit PSDKAdapter(void) noexcept;
		~PSDKAdapter(void) noexcept;
		PSDKAdapter(const PSDKAdapter&) noexcept			= delete;
		PSDKAdapter& operator=(const PSDKAdapter&) noexcept = delete;

		void		 quaternionToEulerAngle(const _DJI T_DjiFcSubscriptionQuaternion& q, double& roll, double& pitch, double& yaw) noexcept;
		void		 acquisitionLoop(void) noexcept;
	};
} // namespace plane::services

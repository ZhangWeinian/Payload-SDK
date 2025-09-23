// manifold2/services/DroneControl/FlyManager.h

#pragma once

#include "define.h"
#include "protocol/DroneDataClass.h"

#include <string_view>
#include <string>
#include <vector>

namespace plane::services
{
	class FlyManager
	{
	public:
		static FlyManager& getInstance(void) noexcept;

		void			   flyToPoint(const protocol::Waypoint& waypoint) const noexcept;
		void			   takeoff(const protocol::TakeoffPayload& takeoffParams) const noexcept;
		void			   goHome(void) const noexcept;
		void			   hover(void) const noexcept;
		void			   land(void) const noexcept;
		void			   waypointFly(_STD string_view kmzFilePath) const noexcept;
		void			   setControlStrategy(int strategyCode) const noexcept;
		void			   flyCircleAroundPoint(const protocol::CircleFlyPayload& circleParams) const noexcept;

		void			   rotateGimbal(double pitch, double yaw) const noexcept;
		void			   rotateGimbalBySpeed(double pitchSpeed, double yawSpeed, double rollSpeed) const noexcept;
		void			   setCameraZoomFactor(const protocol::ZoomControlPayload& zoomParams) const noexcept;
		void			   setCameraStreamSource(_STD string_view source) const noexcept;

		void			   sendRawStickData(int throttle, int yaw, int pitch, int roll) const noexcept;
		void			   enableVirtualStick(bool advancedMode) const noexcept;
		void			   disableVirtualStick(void) const noexcept;
		void			   sendNedVelocityCommand(const protocol::NedVelocityPayload& velocityParams) const noexcept;

	private:
		explicit FlyManager(void) noexcept				  = default;
		~FlyManager(void) noexcept						  = default;
		FlyManager(const FlyManager&) noexcept			  = delete;
		FlyManager& operator=(const FlyManager&) noexcept = delete;
	};
} // namespace plane::services

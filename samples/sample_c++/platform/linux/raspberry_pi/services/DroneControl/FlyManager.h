// raspberry_pi/services/DroneControl/FlyManager.h

#pragma once

#include "protocol/DroneDataClass.h"

#include <string_view>
#include <atomic>
#include <future>
#include <mutex>
#include <string>
#include <vector>

#include "define.h"

namespace plane::services
{
	enum class FlyTaskState
	{
		IDLE,
		RUNNING
	};

	class FlyManager
	{
	public:
		static FlyManager& getInstance(void) noexcept;

		void			   flyToPoint(const plane::protocol::Waypoint& waypoint);
		void			   takeoff(const plane::protocol::TakeoffPayload& takeoffParams);
		void			   goHome(void);
		void			   hover(void);
		void			   land(void);
		void			   waypoint(const _STD vector<_STD uint8_t>& kmzData);
		void			   waypoint(_STD string_view kmzFilePath);
		void			   setControlStrategy(int strategyCode);
		void			   flyCircleAroundPoint(const plane::protocol::CircleFlyPayload& circleParams);

		void			   stopWaypointMission();
		void			   pauseWaypointMission();
		void			   resumeWaypointMission();

		void			   rotateGimbal(double pitch, double yaw) const noexcept;
		void			   rotateGimbalBySpeed(double pitchSpeed, double yawSpeed, double rollSpeed) const noexcept;
		void			   setCameraZoomFactor(const plane::protocol::ZoomControlPayload& zoomParams) const noexcept;
		void			   setCameraStreamSource(_STD string_view source) const noexcept;

		void			   sendRawStickData(int throttle, int yaw, int pitch, int roll) const noexcept;
		void			   enableVirtualStick(bool advancedMode) const noexcept;
		void			   disableVirtualStick(void) const noexcept;
		void			   sendNedVelocityCommand(const plane::protocol::NedVelocityPayload& velocityParams) const noexcept;

	private:
		explicit FlyManager(void) noexcept				  = default;
		~FlyManager(void) noexcept						  = default;
		FlyManager(const FlyManager&) noexcept			  = delete;
		FlyManager& operator=(const FlyManager&) noexcept = delete;
	};
} // namespace plane::services

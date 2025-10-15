// cy_psdk/services/DroneControl/FlyManager.h

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
	class FlyManager
	{
	public:
		static FlyManager& getInstance(void) noexcept;

		void			   flyToPoint(const plane::protocol::Waypoint& waypoint);
		void			   takeoff(const plane::protocol::TakeoffPayload& takeoffParams);
		void			   goHome(void);
		void			   hover(void);
		void			   land(void);
		void			   waypoint(const _DEFINED _KMZ_DATA_TYPE& kmzData);
		void			   waypoint(_STD string_view kmzFilePath);
		void			   setControlStrategy(const _DEFINED _PTZ_CONTROL_STRATEGY_TYPE& strategyCode);
		void			   flyCircleAroundPoint(const plane::protocol::CircleFlyPayload& circleParams);

		void			   stopWaypointMission();
		void			   pauseWaypointMission();
		void			   resumeWaypointMission();

		void			   rotateGimbal(const plane::protocol::GimbalControlPayload& gimbalParams) const noexcept;
		void			   rotateGimbalBySpeed(const plane::protocol::GimbalControlPayload& gimbalParams) const noexcept;
		void			   setCameraZoomFactor(const plane::protocol::ZoomControlPayload& zoomParams) const noexcept;
		void			   setCameraStreamSource(const plane::protocol::ZoomControlPayload& zoomParams) const noexcept;

		void			   sendRawStickData(const plane::protocol::StickDataPayload& stickData) const noexcept;
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

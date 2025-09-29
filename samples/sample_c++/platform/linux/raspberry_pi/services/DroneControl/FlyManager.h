// raspberry_pi/services/DroneControl/FlyManager.h

#pragma once

#include <string_view>
#include <atomic>
#include <future>
#include <mutex>
#include <string>
#include <vector>

#include "protocol/DroneDataClass.h"

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

		void			   flyToPoint(const plane::protocol::Waypoint& waypoint) noexcept;
		void			   takeoff(const plane::protocol::TakeoffPayload& takeoffParams);
		void			   goHome(void) noexcept;
		void			   hover(void) noexcept;
		void			   land(void) noexcept;
		void			   waypointFly(_STD string_view kmzFilePath) noexcept;
		void			   setControlStrategy(int strategyCode) noexcept;
		void			   flyCircleAroundPoint(const plane::protocol::CircleFlyPayload& circleParams) noexcept;

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

		template<typename Func, typename... Args>
		void executeCommand(Func&& command, Args&&... args);

		void interruptCurrentTask(void);

		_STD atomic<FlyTaskState> m_taskState { FlyTaskState::IDLE };
		_STD mutex				  m_taskMutex;
		_STD future<void> m_currentTaskFuture;
	};
} // namespace plane::services

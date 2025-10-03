// raspberry_pi/services/DroneControl/PSDKAdapter/PSDKAdapter.h

#pragma once

#include "protocol/DroneDataClass.h"
#include "protocol/HeartbeatDataClass.h"
#include "services/EventManager/EventManager.h"

#include <dji_fc_subscription.h>
#include <dji_typedef.h>
#include <dji_waypoint_v3.h>

#include <eventpp/eventdispatcher.h>
#include <eventpp/utilities/scopedremover.h>
#include <ThreadPool/ThreadPool.h>

#include <source_location>
#include <string_view>
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include "define.h"

namespace plane::services
{
	class PSDKAdapter
	{
	public:
		static PSDKAdapter& getInstance(void) noexcept;

		_NODISCARD bool		start(void) noexcept;
		void				stop(_STD_CHRONO milliseconds timeout = _STD_CHRONO seconds(5)) noexcept;

		_NODISCARD bool		setup(void) noexcept;
		void				cleanup(void) noexcept;

		_NODISCARD plane::protocol::StatusPayload getLatestStatusPayload(void) const noexcept;
		_NODISCARD _STD_CHRONO steady_clock::time_point getLastUpdateTime(void) const noexcept;

		enum class PSDKEvent
		{
			TelemetryUpdated,
			MissionStateChanged,
			ActionStateChanged
		};

		using EventData		  = _STD variant<plane::protocol::StatusPayload, _DJI T_DjiWaypointV3MissionState, _DJI T_DjiWaypointV3ActionState>;
		using EventDispatcher = _EVENTPP EventDispatcher<PSDKEvent, void(const EventData&)>;

	private:
		explicit PSDKAdapter(void) noexcept;
		~PSDKAdapter(void) noexcept;
		PSDKAdapter(const PSDKAdapter&) noexcept			= delete;
		PSDKAdapter& operator=(const PSDKAdapter&) noexcept = delete;

		template<typename CommandLogic>
		_STD future<_DJI T_DjiReturnCode> executePsdkCommand(CommandLogic&&				 logic,
															 const _STD source_location& location = _STD source_location::current());

		_STD future<_DJI T_DjiReturnCode> executeWaypointAction(_DJI E_DjiWaypointV3Action	action,
																const _STD source_location& location = _STD source_location::current());

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

		void quaternionToEulerAngle(const _DJI T_DjiFcSubscriptionQuaternion& q, double& roll, double& pitch, double& yaw) noexcept;
		void acquisitionLoop(void) noexcept;

		void missionStateCallback(_DJI T_DjiWaypointV3MissionState missionState);
		void actionStateCallback(_DJI T_DjiWaypointV3ActionState actionState);
		static _DJI T_DjiReturnCode missionStateCallbackEntry(_DJI T_DjiWaypointV3MissionState missionState);
		static _DJI T_DjiReturnCode actionStateCallbackEntry(_DJI T_DjiWaypointV3ActionState actionState);

		_NODISCARD _STD future<_DJI T_DjiReturnCode> takeoff(const plane::protocol::TakeoffPayload& takeoffParams);
		_NODISCARD _STD future<_DJI T_DjiReturnCode> goHome(void);
		_NODISCARD _STD future<_DJI T_DjiReturnCode> hover(void);
		_NODISCARD _STD future<_DJI T_DjiReturnCode> land(void);
		_NODISCARD _STD future<_DJI T_DjiReturnCode> waypointV3(const _STD vector<_STD uint8_t>& kmzData);
		_NODISCARD _STD future<_DJI T_DjiReturnCode> setControlStrategy(int strategyCode);
		_NODISCARD _STD future<_DJI T_DjiReturnCode> flyCircleAroundPoint(const plane::protocol::CircleFlyPayload& circleParams);

		void										 rotateGimbal(const plane::protocol::GimbalControlPayload& payload);
		void										 setCameraZoomFactor(const plane::protocol::ZoomControlPayload& payload);
		void										 setCameraStreamSource(const _STD string& source);
		void										 sendRawStickData(const plane::protocol::StickDataPayload& payload);
		void										 enableVirtualStick(const plane::protocol::StickModeSwitchPayload& payload);
		void										 disableVirtualStick(const plane::protocol::StickModeSwitchPayload& payload);
		void										 sendNedVelocityCommand(const plane::protocol::NedVelocityPayload& payload);

		_NODISCARD _STD future<_DJI T_DjiReturnCode> stopWaypointMission(void);
		_NODISCARD _STD future<_DJI T_DjiReturnCode> pauseWaypointMission(void);
		_NODISCARD _STD future<_DJI T_DjiReturnCode> resumeWaypointMission(void);

		void										 commandProcessingLoop(void);

		template<typename PayloadType, typename Func>
		void registerCommandListener(plane::services::EventManager::CommandEvent event, Func func);

		_STD unique_ptr<_THREADPOOL ThreadPool> command_pool_ {};
		_STD mutex								psdk_command_mutex_ {};
		_STD thread								acquisition_thread_ {};
		_STD atomic<bool> run_acquisition_ { false };
		_STD atomic<bool> is_stopping_ { false };
		_STD unique_ptr<_STD promise<_DJI T_DjiReturnCode>> mission_completion_promise_ {};
		SubscriptionStatus									sub_status_ {};
		mutable _STD mutex									payload_mutex_ {};
		mutable _STD mutex									health_mutex_ {};
		plane::protocol::StatusPayload						latest_payload_ {};
		_STD_CHRONO steady_clock::time_point last_update_time_ {};
		_STD unique_ptr<_EVENTPP ScopedRemover<plane::services::EventManager::CommandQueue>> command_queue_remover_ {};
		_STD thread																			 command_processing_thread_ {};
		_STD atomic<bool>	  run_command_processing_ { false };
		constexpr static auto ACQUISITION_INTERVAL { _STD_CHRONO milliseconds(20) };
	};
} // namespace plane::services

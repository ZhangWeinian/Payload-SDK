// raspberry_pi/EventManager/services/EventManager.h

#pragma once

#include "protocol/DroneDataClass.h"
#include "protocol/HeartbeatDataClass.h"

#include <dji_typedef.h>
#include <dji_waypoint_v3.h>

#include <eventpp/eventdispatcher.h>
#include <eventpp/eventqueue.h>

#include <string>
#include <variant>

namespace plane::services
{
	class EventManager
	{
	public:
		// 命令事件
		enum class CommandEvent
		{
			// 任务指令
			Takeoff,
			GoHome,
			Hover,
			Land,
			WaypointMission,
			StopWaypointMission,
			PauseWaypointMission,
			ResumeWaypointMission,
			SetControlStrategy,
			FlyCircleAroundPoint,

			// 即时指令
			RotateGimbal,
			RotateGimbalBySpeed,
			SetCameraZoomFactor,
			SetCameraStreamSource,
			SendRawStickData,
			EnableVirtualStick,
			DisableVirtualStick,
			SendNedVelocityCommand
		};
		using CommandData = _STD variant<_STD monostate, // 用于没有参数的命令

										 // 对应 DroneDataClass 中的结构体
										 plane::protocol::TakeoffPayload,		  // 起飞
										 plane::protocol::CircleFlyPayload,		  // 围绕点飞行
										 plane::protocol::GimbalControlPayload,	  // 云台控制
										 plane::protocol::ZoomControlPayload,	  // 相机变焦控制
										 plane::protocol::StickDataPayload,		  // 发送摇杆数据
										 plane::protocol::StickModeSwitchPayload, // 启用/禁用虚拟摇杆
										 plane::protocol::NedVelocityPayload,	  // 发送 NED 速度指令

										 // 对于没有直接对应结构体的，使用基本类型
										 _STD vector<uint8_t>, // KMZ 数据
										 int,				   // 设置控制策略
										 _STD string		   // 设置视频源
										 >;

		// PSDK 状态事件
		enum class PSDKEvent
		{
			TelemetryUpdated,
			MissionStateChanged,
			ActionStateChanged
		};
		using PSDKEventData	   = _STD		 variant<protocol::StatusPayload, T_DjiWaypointV3MissionState, T_DjiWaypointV3ActionState>;

		using CommandQueue	   = _EVENTPP	  EventQueue<CommandEvent, void(const CommandEvent&, const CommandData&)>;
		using StatusDispatcher = _EVENTPP EventDispatcher<PSDKEvent, void(const PSDKEventData&)>;

		static EventManager&			  getInstance(void) noexcept;

		void							  publishCommand(CommandEvent event, const CommandData& data);
		void							  publishStatus(PSDKEvent event, const PSDKEventData& data);

		CommandQueue&					  getCommandQueue(void) noexcept
		{
			return command_queue_;
		}

		StatusDispatcher& getStatusDispatcher(void) noexcept
		{
			return status_dispatcher_;
		}

	private:
		explicit EventManager(void) noexcept			= default;
		~EventManager(void) noexcept					= default;
		EventManager(const EventManager&)				= delete;
		EventManager&	 operator=(const EventManager&) = delete;

		CommandQueue	 command_queue_ {};
		StatusDispatcher status_dispatcher_ {};
	};
} // namespace plane::services

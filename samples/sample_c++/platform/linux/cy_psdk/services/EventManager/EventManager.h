// cy_psdk/EventManager/services/EventManager.h

#pragma once

#include "protocol/DroneDataClass.h"
#include "protocol/HeartbeatDataClass.h"

#include <dji_typedef.h>
#include <dji_waypoint_v3.h>

#include <eventpp/eventdispatcher.h>
#include <eventpp/eventqueue.h>

#include <chrono>
#include <string>
#include <variant>
#include <vector>

#include "define.h"

namespace plane::services
{
	class EventManager
	{
	public:
		// 命令事件
		enum class CommandEvent
		{
			// 任务指令
			Takeoff,			   // 起飞
			GoHome,				   // 返航
			Hover,				   // 悬停
			Land,				   // 降落
			WaypointMission,	   // 航点任务
			StopWaypointMission,   // 停止航点任务
			PauseWaypointMission,  // 暂停航点任务
			ResumeWaypointMission, // 恢复航点任务
			FlyCircleAroundPoint,  // 环绕飞行

			// 即时指令
			RotateGimbal,			// 云台控制
			RotateGimbalBySpeed,	// 云台速度控制
			SetCameraZoomFactor,	// 相机变焦
			SetControlStrategy,		// 设置云台控制策略
			SetCameraStreamSource,	// 切换视频源
			SendRawStickData,		// 发送虚拟摇杆数据
			EnableVirtualStick,		// 启用虚拟摇杆
			DisableVirtualStick,	// 禁用虚拟摇杆
			SendNedVelocityCommand, // 发送 NED 速度指令
		};

		using CommandData  = _STD	   variant<_STD monostate, // 用于没有参数的命令

											   // 对应 DroneDataClass 中的结构体
											   plane::protocol::TakeoffPayload,			// 起飞
											   plane::protocol::CircleFlyPayload,		// 围绕点飞行
											   plane::protocol::GimbalControlPayload,	// 云台控制
											   plane::protocol::ZoomControlPayload,		// 相机变焦控制
											   plane::protocol::StickDataPayload,		// 发送摇杆数据
											   plane::protocol::StickModeSwitchPayload, // 启用/禁用虚拟摇杆
											   plane::protocol::NedVelocityPayload,		// 发送 NED 速度指令

											   // 对于没有直接对应结构体的，使用基本类型
											   _DEFINED _STD vector<_STD uint8_t>,
											   _DEFINED int,
											   _DEFINED _STD string>;
		using CommandQueue = _EVENTPP EventQueue<CommandEvent, void(const CommandEvent&, const CommandData&)>;

		// PSDK 状态事件
		enum class PSDKEvent
		{
			TelemetryUpdated,
			MissionStateChanged,
			ActionStateChanged,
			HealthPing,
			HealthStatusUpdated
		};
		using PSDKEventData	   = _STD		 variant<plane::protocol::StatusPayload,
													 _DJI		 T_DjiWaypointV3MissionState,
													 _DJI		 T_DjiWaypointV3ActionState,
													 _STD_CHRONO steady_clock::time_point,
													 plane::protocol::HealthStatusPayload>;

		using StatusDispatcher = _EVENTPP EventDispatcher<PSDKEvent, void(const PSDKEventData&)>;

		enum class SystemEvent
		{
			HeartbeatTick
		};

		using SystemEventData  = _STD	   variant<_STD monostate>;
		using SystemDispatcher = _EVENTPP EventDispatcher<SystemEvent, void(const SystemEventData&)>;

		static EventManager&			  getInstance(void) noexcept;

		void							  publishCommand(CommandEvent event, const CommandData& data);
		void							  publishStatus(PSDKEvent event, const PSDKEventData& data);
		void							  publishSystemEvent(SystemEvent event, const SystemEventData& data = _STD monostate {});

		// 获取各事件队列/分发器的引用
		CommandQueue& getCommandQueue(void) noexcept
		{
			return this->command_queue_;
		}

		StatusDispatcher& getStatusDispatcher(void) noexcept
		{
			return this->status_dispatcher_;
		}

		SystemDispatcher& getSystemDispatcher(void) noexcept
		{
			return this->system_dispatcher_;
		}

	private:
		explicit EventManager(void) noexcept			= default;
		~EventManager(void) noexcept					= default;
		EventManager(const EventManager&)				= delete;
		EventManager&	 operator=(const EventManager&) = delete;

		CommandQueue	 command_queue_ {};
		StatusDispatcher status_dispatcher_ {};
		SystemDispatcher system_dispatcher_ {};
	};
} // namespace plane::services

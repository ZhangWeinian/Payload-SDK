// cy_psdk/services/PSDK/PSDKAdapter.h

#pragma once

#include "protocol/DroneDataClass.h"
#include "protocol/HeartbeatDataClass.h"
#include "services/EventManager/EventManager.h"

#include <dji_fc_subscription.h>
#include <dji_hms_manager.h>
#include <dji_typedef.h>
#include <dji_waypoint_v3.h>

#include <eventpp/eventdispatcher.h>
#include <eventpp/utilities/scopedremover.h>
#include <ThreadPool/ThreadPool.h>

#include <source_location>
#include <string_view>
#include <unordered_map>
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

		// 启动 PSDK 适配器服务，这是一个幂等的操作
		_NODISCARD bool start(void) noexcept;

		// 停止 PSDK 适配器服务，这是一个幂等的操作
		void stop(_STD_CHRONO milliseconds timeout = _STD_CHRONO seconds(5)) noexcept;

		// 获取最新的状态数据载荷的一个副本
		_NODISCARD plane::protocol::StatusPayload getLatestStatusPayload(void) const noexcept;

	private:
		explicit PSDKAdapter(void) noexcept;
		~PSDKAdapter(void) noexcept;
		PSDKAdapter(const PSDKAdapter&) noexcept			= delete;
		PSDKAdapter& operator=(const PSDKAdapter&) noexcept = delete;

		// PSDK 适配器状态
		enum class State
		{
			STOPPED,  // 已停止
			STARTING, // 启动中
			RUNNING,  // 运行中
			STOPPING  // 停止中
		};

		// 订阅 PSDK 数据状态、添加 PSDK 状态订阅
		_NODISCARD bool setup(void) noexcept;

		// 清理 PSDK 适配器状态、移除 PSDK 状态订阅
		void cleanup(void) noexcept;

		// 从 PSDK 四元数到欧拉角的转换，将 PSDK 飞控订阅的四元数数据转换为以度为单位的 roll（横滚）、pitch（俯仰）、yaw（偏航）三个角度
		void convertQuaternionToEulerAngle(const _DJI T_DjiFcSubscriptionQuaternion& q, double& roll, double& pitch, double& yaw) noexcept;

		// 周期性地从 PSDK 订阅的飞控数据主题中拉取最新状态，转换为统一的 StatusPayload ，并通过事件总线发布
		void acquisitionLoop(void) noexcept;

		// 在 PSDK 航线任务状态发生变化时，将状态信息通过事件总线广播给系统其他模块
		void missionStateCallback(_DJI T_DjiWaypointV3MissionState missionState);

		// 作为 PSDK 所要求的 C 兼容回调函数，将 C 接口调用桥接到 C++ 成员函数 missionStateCallback ，并返回 PSDK 期望的成功码
		static _DJI T_DjiReturnCode missionStateCallbackEntry(_DJI T_DjiWaypointV3MissionState missionState);

		// 处理 PSDK 航线中某个具体动作执行状态变化，并通过事件总线广播给系统其他模块
		void actionStateCallback(_DJI T_DjiWaypointV3ActionState actionState);

		// 作为 PSDK 所要求的 C 兼容回调函数，将 C 接口调用桥接到 C++ 成员函数 actionStateCallback ，并返回 PSDK 期望的成功码
		static _DJI T_DjiReturnCode actionStateCallbackEntry(_DJI T_DjiWaypointV3ActionState actionState);

		// 处理 PSDK HMS 信息更新，并通过事件总线广播给系统其他模块
		void hmsInfoCallback(_DJI T_DjiHmsInfoTable hmsInfoTable);

		// 作为 PSDK 所要求的 C 兼容回调函数，将 C 接口调用桥接到 C++ 成员函数 hmsInfoCallback ，并返回 PSDK 期望的成功码
		static _DJI T_DjiReturnCode hmsInfoCallbackEntry(_DJI T_DjiHmsInfoTable hmsInfoTable);

		// PSDK 命令执行器，可以安全地在线程池中异步执行一个 PSDK 命令，并返回一个 _STD future 用于获取执行结果
		template<typename CommandLogic>
		_STD future<_DJI T_DjiReturnCode> executePsdkCommandAsync(CommandLogic&&			  logic,
																  const _STD source_location& location = _STD source_location::current());

		// 异步执行 PSDK 航线动作命令，封装 DjiWaypointV3_Action 为一个异步任务
		_STD future<_DJI T_DjiReturnCode> executeWaypointActionAsync(_DJI E_DjiWaypointV3Action	 action,
																	 const _STD source_location& location = _STD source_location::current());

		// 异步执行起飞指令，使飞行器从地面垂直升空至安全高度
		_NODISCARD _STD future<_DJI T_DjiReturnCode> takeoffAsync(const plane::protocol::TakeoffPayload& takeoffParams);

		// 异步执行返航指令，使飞行器自动返回起飞点并降落
		_NODISCARD _STD future<_DJI T_DjiReturnCode> goHomeAsync(void);

		// 异步执行悬停指令，使飞行器在当前位置紧急制动并保持悬停
		_NODISCARD _STD future<_DJI T_DjiReturnCode> hoverAsync(void);

		// 异步执行降落指令，使飞行器从当前位置垂直下降并着陆
		_NODISCARD _STD future<_DJI T_DjiReturnCode> landAsync(void);

		// 异步上传并执行 KMZ 格式的航线任务
		_NODISCARD _STD future<_DJI T_DjiReturnCode> waypointAsync(const _DEFINED _KMZ_DATA_TYPE& kmzData);

		// 异步设置飞行控制策略
		_NODISCARD _STD future<_DJI T_DjiReturnCode> setControlStrategyAsync(const _DEFINED _PTZ_CONTROL_STRATEGY_TYPE& strategyCode);

		// 异步执行环绕指定地理点飞行的任务
		_NODISCARD _STD future<_DJI T_DjiReturnCode> selfPOIAsync(const plane::protocol::CircleFlyPayload& circleParams);

		// 执行云台角度控制指令（设置目标角度或角速度）
		void rotateGimbal(const plane::protocol::GimbalControlPayload& payload);

		// 设置相机变焦倍数
		void setCameraZoomFactor(const plane::protocol::ZoomControlPayload& payload);

		// 切换相机视频流源（如主相机、热成像等）
		void setCameraStreamSource(const _DEFINED _VIDEO_SOURCE_TYPE& source);

		// 发送原始虚拟摇杆数据（用于精细控制飞行器运动）
		void sendRawStickData(const plane::protocol::StickDataPayload& payload);

		// 启用虚拟摇杆控制模式
		void enableVirtualStick(const plane::protocol::StickModeSwitchPayload& payload);

		// 禁用虚拟摇杆控制模式
		void disableVirtualStick(const plane::protocol::StickModeSwitchPayload& payload);

		// 同步发送 NED 坐标系下的速度指令（北-东-地）
		void sendNedVelocityCommand(const plane::protocol::NedVelocityPayload& payload);

		// 异步停止当前正在执行的航线任务
		_NODISCARD _STD future<_DJI T_DjiReturnCode> stopWaypointMissionAsync(void);

		// 异步暂停当前正在执行的航线任务
		_NODISCARD _STD future<_DJI T_DjiReturnCode> pauseWaypointMissionAsync(void);

		// 异步恢复已暂停的航线任务
		_NODISCARD _STD future<_DJI T_DjiReturnCode> resumeWaypointMissionAsync(void);

		// 持续监听并处理命令事件的主循环
		void commandProcessingLoop(void);

		// 注册一个回调函数，用于响应指定类型的命令事件
		template<typename PayloadType, typename Func>
		void registerCommandListener(plane::services::EventManager::CommandEvent event, Func func);

		friend class PSDKManager;

		// 当前适配器状态
		struct SubscriptionStatus
		{
			/*!
			* 飞行器融合位置主题名称。请参考 ::T_DjiFcSubscriptionPositionFused 了解数据结构信息。
			*
			* @warning 请注意，如果 GPS 信号较弱（参见下方的 visibleSatelliteNumber），则纬度/经度值将不会更新，但高度仍可能更新。
			*          目前无法判断纬度/经度的更新是否可靠。
			*
			* @details 此主题最重要的组成部分是 T_DjiFcSubscriptionPositionFused::visibleSatelliteNumber。
			*          请使用该值来跟踪您的 GPS 卫星覆盖情况，并建立一些启发式方法，以便在可能失去 GPS 更新时提前做出预判。
			*/
			bool positionFused { false };

			/*!
			* @brief 飞行器融合高度主题名称。融合高度主题提供飞行器相对于海平面的融合高度。
			*        请参考 ::T_DjiFcSubscriptionAltitudeFused 了解数据结构信息。
			*
			* 单位 m
			* 数据结构 \ref T_DjiFcSubscriptionAltitudeFused
			*/
			bool altitudeFused { false };

			/*!
			* @brief 提供飞行器上次起飞时相对于海平面的高度。
			*
			* @details 这是飞控系统融合输出的结果，同时也使用了国际标准大气（ICAO）模型。
			*          ICAO 模型定义在 15°C 时海平面标准气压为 1013.25 mBar，温度递减率为每 1000 米下降 6.5°C。
			*          在您的实际场景中，起飞点的气压可能高于 1013.25 mBar。例如，气象站显示旧金山国际机场（SFO）近期记录的气压为 1027.1 mBar。
			*          SFO 实际海拔约为 4 米，但若使用 ICAO 模型计算气压高度，则对应约为 -114 米。您可以使用在线计算器来估算您所在区域的气压高度。
			*
			*          影响高度读数的另一个因素是气压计的制造差异——在同一物理位置，两架不同的飞行器之间出现 ±30 米的高度偏差并不罕见。
			*          对于同一架飞行器，这些读数通常是稳定的，因此如果您的代码依赖于绝对高度值的准确性，您需要对系统进行偏移校准。
			*
			* @note 该值在每次无人机起飞时更新。
			*
			* 单位 m
			* 数据结构 \ref T_DjiFcSubscriptionAltitudeOfHomePoint
			*/
			bool altitudeOfHomepoint { false };

			/*!
			* @brief 飞行器四元数主题名称。四元数主题提供从飞行器机体坐标系（FRD）到地面坐标系（NED）的旋转关系。
			*        请参考 ::T_DjiFcSubscriptionQuaternion 了解数据结构信息。
			*
			* @details DJI 的四元数采用 Hamilton 约定（q0 = w, q1 = x, q2 = y, q3 = z）。
			*
			* 数据结构 \ref T_DjiFcSubscriptionQuaternion
			*/
			bool quaternion { false };

			/*!
			* @brief 飞行器速度主题名称。速度主题提供飞行器在固定于地面的 NEU 坐标系中的速度。
			*        请参考 ::T_DjiFcSubscriptionVelocity 了解数据结构信息。
			*
			* @warning 请注意，此数据并非采用常规的右手坐标系。
			*
			* @details 该速度数据是飞行器融合输出的结果。原始输出是在右手 NED 坐标系中，但在发布到此主题前，Z 轴速度的符号已被翻转。
			*          因此，如果您希望获得 NED 坐标系下的速度，只需将 Z 轴速度值再次取反即可。
			*          在此基础上，您可以通过旋转将其转换为任意右手坐标系。
			*
			* 数据结构 \ref T_DjiFcSubscriptionVelocity
			*/
			bool velocity { false };

			/*!
			* @brief 电池信息主题名称。请参考 ::T_DjiFcSubscriptionWholeBatteryInfo 了解数据结构信息。
			*
			* 数据结构 \ref T_DjiFcSubscriptionWholeBatteryInfo
			*/
			bool batteryInfo { false };

			/*!
			* @brief 提供 1 号云台的俯仰（pitch）、横滚（roll）、偏航（yaw）角度，最高更新频率达 50Hz 。
			*
			* @details 云台角度的参考坐标系是附着于云台的 NED 坐标系。
			*          该主题使用了一个过于通用的数据结构 Vector3f 。各分量含义如下:
			*          |  数据结构元素  |      含义      |
			*          |  Vector3f.x  |  俯仰角（pitch）|
			*          |  Vector3f.y  |  横滚角（roll） |
			*          |  Vector3f.z  |  偏航角（yaw）  |
			*
			* 单位 deg（度）
			* 数据结构 \ref T_DjiFcSubscriptionGimbalAngles
			* 参见 \ref TOPIC_GIMBAL_STATUS, \ref TOPIC_GIMBAL_CONTROL_MODE
			*/
			bool gimbalAngles { false };
		} sub_status_;

		mutable _STD mutex payload_mutex_ {};
		_STD mutex		   psdk_command_mutex_ {};
		_STD mutex		   hms_mutex_ {};
		_STD thread		   acquisition_thread_ {};
		_STD thread		   command_processing_thread_ {};
		_STD vector<_STD uint32_t> last_hms_error_codes_ {};
		_STD atomic<State> state_ { State::STOPPED };
		_STD atomic<bool> run_acquisition_ { false };
		_STD atomic<bool> run_command_processing_ { false };
		_STD unique_ptr<_THREADPOOL ThreadPool> command_pool_ {};
		_STD unique_ptr<_STD promise<_DJI T_DjiReturnCode>> mission_completion_promise_ {};
		_STD unique_ptr<_EVENTPP ScopedRemover<plane::services::EventManager::CommandQueue>> command_queue_remover_ {};
		plane::protocol::StatusPayload														 latest_payload_ {};
		constexpr static auto ACQUISITION_INTERVAL { _STD_CHRONO milliseconds(20) };
	};
} // namespace plane::services

// raspberry_pi/services/DroneControl/FlyManager.cpp

#include "services/DroneControl/FlyManager.h"

#include "services/DroneControl/PSDKAdapter/PSDKAdapter.h"
#include "utils/Logger.h"

namespace plane::services
{
	FlyManager& FlyManager::getInstance(void) noexcept
	{
		static FlyManager instance {};
		return instance;
	}

	void FlyManager::interruptCurrentTask(void)
	{
		if (m_taskState == FlyTaskState::RUNNING)
		{
			LOG_INFO("检测到有任务正在运行，正在发送中断指令...");
			plane::services::PSDKAdapter::getInstance().hover();
			LOG_INFO("中断指令已提交到后台队列。");
		}
	}

	template<typename Func, typename... Args>
	void FlyManager::executeCommand(Func&& command, Args&&... args)
	{
		_STD lock_guard<_STD mutex> lock(m_taskMutex);

		// 发送中断指令（如果需要）
		// interruptCurrentTask();

		LOG_INFO("正在向 PSDKAdapter 提交新任务...");
		auto commandFuture = _STD invoke(command, plane::services::PSDKAdapter::getInstance(), _STD forward<Args>(args)...);
		m_taskState		   = FlyTaskState::RUNNING;

		_STD thread(
			[this, f = _STD move(commandFuture)]() mutable
			{
				f.wait();

				FlyTaskState expected { FlyTaskState::RUNNING };
				if (m_taskState.compare_exchange_strong(expected, FlyTaskState::IDLE))
				{
					LOG_INFO("FlyManager: 一个后台 PSDK 任务已执行完毕，状态已更新为 IDLE。");
				}
				else
				{
					LOG_INFO("FlyManager: 一个后台 PSDK 任务已执行完毕，但状态已被新任务覆盖，无需更新。");
				}
			})
			.detach();
	}

	void FlyManager::flyToPoint(const plane::protocol::Waypoint& waypoint) noexcept
	{
		LOG_INFO("执行【单点飞行】: Lon={}, Lat={}, Alt={}", waypoint.JD, waypoint.WD, waypoint.GD);
		// TODO: 调用 PSDK 的单点飞行 API
	}

	void FlyManager::waypointFly(_STD string_view kmzFilePath) noexcept
	{
		LOG_INFO("执行【航线】");
		executeCommand(&plane::services::PSDKAdapter::waypointV3, _STD string(kmzFilePath));
	}

	void FlyManager::takeoff(const plane::protocol::TakeoffPayload& takeoffParams)
	{
		LOG_INFO("执行【起飞】");
		executeCommand(&plane::services::PSDKAdapter::takeoff, takeoffParams);
	}

	void FlyManager::goHome(void) noexcept
	{
		LOG_INFO("执行【返航】");
		executeCommand(&plane::services::PSDKAdapter::goHome);
	}

	void FlyManager::hover(void) noexcept
	{
		LOG_INFO("执行【悬停】");
		_STD lock_guard<_STD mutex> lock(m_taskMutex);
		interruptCurrentTask();
	}

	void FlyManager::land(void) noexcept
	{
		LOG_INFO("执行【降落】");
		executeCommand(&plane::services::PSDKAdapter::land);
	}

	void FlyManager::setControlStrategy(int strategyCode) noexcept
	{
		LOG_INFO("执行【设置云台控制策略】, 策略代码: {}", strategyCode);
		executeCommand(&plane::services::PSDKAdapter::setControlStrategy, strategyCode);
	}

	void FlyManager::flyCircleAroundPoint(const plane::protocol::CircleFlyPayload& circleParams) noexcept
	{
		LOG_INFO("执行【环绕飞行】");
		executeCommand(&plane::services::PSDKAdapter::flyCircleAroundPoint, circleParams);
	}

	void FlyManager::rotateGimbal(double pitch, double yaw) const noexcept
	{
		LOG_INFO("执行【云台角度控制】: Pitch={}, Yaw={}", pitch, yaw);
		// TODO: 调用 PSDK 的云台角度控制 API
	}

	void FlyManager::rotateGimbalBySpeed(double pitchSpeed, double yawSpeed, double rollSpeed) const noexcept
	{
		LOG_INFO("执行【云台速度控制】: PitchSpeed={}, YawSpeed={}", pitchSpeed, yawSpeed);
		// TODO: 调用 PSDK 的云台速度控制 API
	}

	void FlyManager::setCameraZoomFactor(const plane::protocol::ZoomControlPayload& zoomParams) const noexcept
	{
		LOG_INFO("执行【相机变焦】: Factor={}", zoomParams.BJB.value_or(1.0));
		// TODO: 调用 PSDK 的相机变焦 API
	}

	void FlyManager::setCameraStreamSource(_STD string_view source) const noexcept
	{
		LOG_INFO("执行【切换视频源】: Source={}", source);
		// TODO: 调用 PSDK 的相机视频源切换 API
	}

	void FlyManager::sendRawStickData(int throttle, int yaw, int pitch, int roll) const noexcept
	{
		LOG_DEBUG("发送【虚拟摇杆数据】: T:{} Y:{} P:{} R:{}", throttle, yaw, pitch, roll);
		// TODO: 调用 PSDK 的虚拟摇杆数据发送 API
	}

	void FlyManager::enableVirtualStick(bool advancedMode) const noexcept
	{
		LOG_INFO("执行【开启虚拟摇杆】, 高级模式: {}", advancedMode);
		// TODO: 调用 PSDK 的开启虚拟摇杆 API
	}

	void FlyManager::disableVirtualStick(void) const noexcept
	{
		LOG_INFO("执行【关闭虚拟摇杆】");
		// TODO: 调用 PSDK 的关闭虚拟摇杆 API
	}

	void FlyManager::sendNedVelocityCommand(const plane::protocol::NedVelocityPayload& velocityParams) const noexcept
	{
		LOG_DEBUG("发送【NED 速度指令】: N:{:.2f}, E:{:.2f}, D:{:.2f}, Yaw:{:.2f}",
				  velocityParams.SDN,
				  velocityParams.SDD,
				  velocityParams.SDX,
				  velocityParams.PHJ);
		// TODO: 调用 PSDK 的速度控制 API
	}
} // namespace plane::services

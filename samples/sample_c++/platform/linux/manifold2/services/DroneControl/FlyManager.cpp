// manifold2/services/DroneControl/FlyManager.cpp

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

	void FlyManager::flyToPoint(const protocol::Waypoint& waypoint) const noexcept
	{
		LOG_INFO("执行【飞向单点】: Lon={}, Lat={}, Alt={}", waypoint.JD, waypoint.WD, waypoint.GD);
		// TODO: 调用 PSDK 的单点飞行 API
	}

	void FlyManager::waypointFly(_STD string_view kmzFilePath) const noexcept
	{
		LOG_INFO("正在向 PSDKAdapter 提交 KMZ 航线任务...");
		PSDKAdapter::getInstance().waypointV3(_STD string(kmzFilePath));
	}

	void FlyManager::takeoff(const protocol::TakeoffPayload& takeoffParams) const
	{
		LOG_INFO("正在向 PSDKAdapter 提交起飞指令...");
		PSDKAdapter::getInstance().takeoff(takeoffParams);
	}

	void FlyManager::goHome(void) const noexcept
	{
		LOG_INFO("正在向 PSDKAdapter 提交返航指令...");
		PSDKAdapter::getInstance().goHome();
	}

	void FlyManager::hover(void) const noexcept
	{
		LOG_INFO("正在向 PSDKAdapter 提交悬停指令...");
		PSDKAdapter::getInstance().hover();
	}

	void FlyManager::land(void) const noexcept
	{
		LOG_INFO("正在向 PSDKAdapter 提交降落指令...");
		PSDKAdapter::getInstance().land();
	}

	void FlyManager::setControlStrategy(int strategyCode) const noexcept
	{
		LOG_INFO("执行【设置云台控制策略】, 策略代码: {}", strategyCode);
		PSDKAdapter::getInstance().setControlStrategy(strategyCode);
	}

	void FlyManager::flyCircleAroundPoint(const protocol::CircleFlyPayload& circleParams) const noexcept
	{
		LOG_INFO("执行【环绕飞行】");
		PSDKAdapter::getInstance().flyCircleAroundPoint(circleParams);
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

	void FlyManager::setCameraZoomFactor(const protocol::ZoomControlPayload& zoomParams) const noexcept
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

	void FlyManager::sendNedVelocityCommand(const protocol::NedVelocityPayload& velocityParams) const noexcept
	{
		LOG_DEBUG("发送【NED 速度指令】: N:{:.2f}, E:{:.2f}, D:{:.2f}, Yaw:{:.2f}",
				  velocityParams.SDN,
				  velocityParams.SDD,
				  velocityParams.SDX,
				  velocityParams.PHJ);
		// TODO: 调用 PSDK 的速度控制 API
	}
} // namespace plane::services

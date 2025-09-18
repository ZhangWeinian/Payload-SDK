#include "services/drone_control/FlyManager/FlyManager.h"
#include "services/drone_control/PSDKAdapter/PSDKAdapter.h"
#include "utils/Logger/Logger.h"

namespace plane::services
{
	FlyManager& FlyManager::getInstance(void) noexcept
	{
		static FlyManager instance;
		return instance;
	}

	void FlyManager::flyToPoint(const protocol::Waypoint& waypoint) const noexcept
	{
		LOG_INFO("执行【飞向单点】: Lon={}, Lat={}, Alt={}", waypoint.JD, waypoint.WD, waypoint.GD);
		// TODO: 调用 PSDK 的单点飞行 API
	}

	void FlyManager::waypointFly(const std::vector<protocol::Waypoint>& waypoints) const noexcept
	{
		LOG_INFO("执行【航线飞行】, 共 {} 个航点", waypoints.size());
		// TODO: 将 waypoints 转换为 PSDK 的航线格式
		// TODO: 调用 PSDK 的航线上传和启动 API
	}

	std::vector<protocol::Waypoint> FlyManager::optimizeWaypoints(const std::vector<protocol::Waypoint>& waypoints) const noexcept
	{
		return std::vector<protocol::Waypoint>();
	}

	void FlyManager::takeoff(const protocol::TakeoffPayload& takeoffParams) const noexcept
	{
		LOG_INFO("执行【起飞】...");
		if (T_DjiReturnCode rc { PSDKAdapter::getInstance().takeoff(takeoffParams) }; rc == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
		{
			LOG_INFO("起飞指令已成功发送至飞机。");
			// TODO: 更新内部状态为“正在起飞”，并通过 MQTT 上报
		}
		else
		{
			LOG_ERROR("起飞指令发送失败！");
			// TODO: 通过 MQTT 上报错误
		}
	}

	void FlyManager::goHome(void) const noexcept
	{
		LOG_INFO("执行【返航】");
		PSDKAdapter::getInstance().goHome();
	}

	void FlyManager::hover(void) const noexcept
	{
		LOG_INFO("执行【悬停】");
		// TODO: 调用 PSDK 的悬停 API
	}

	void FlyManager::setControlStrategy(int strategyCode) const noexcept
	{
		LOG_INFO("执行【设置云台控制策略】, 策略代码: {}", strategyCode);
		// TODO: 调用 PSDK 的 setControlStrategy API
	}

	void FlyManager::flyCircleAroundPoint(const protocol::CircleFlyPayload& circleParams) const noexcept
	{
		LOG_INFO("执行【环绕飞行】");
		// TODO: 调用 PSDK 的热点环绕或兴趣点环绕 API
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

	void FlyManager::setCameraStreamSource(const std::string& source) const noexcept
	{
		LOG_INFO("执行【切换视频源】: Source={}", source);
		// TODO: 调用 PSDK 的相机视频源切换 API
	}

	// --- 虚拟摇杆实现 ---
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

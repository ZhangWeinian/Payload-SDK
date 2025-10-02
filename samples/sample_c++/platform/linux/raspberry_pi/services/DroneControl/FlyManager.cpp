// raspberry_pi/services/DroneControl/FlyManager.cpp

#include "FlyManager.h"

#include "services/DroneControl/CommandQueue.h"
#include "services/DroneControl/PSDKAdapter/PSDKAdapter.h"
#include "utils/JsonConverter/JsonToKmz.h"
#include "utils/Logger.h"

#include <filesystem>
#include <fstream>

namespace plane::services
{
	FlyManager& FlyManager::getInstance(void) noexcept
	{
		static FlyManager instance {};
		return instance;
	}

	void FlyManager::flyToPoint(const plane::protocol::Waypoint& waypoint)
	{
		LOG_INFO("执行【单点飞行】: Lon={}, Lat={}, Alt={}", waypoint.JD, waypoint.WD, waypoint.GD);
		// TODO: 调用 PSDK 的单点飞行 API
	}

	void FlyManager::waypoint(const _STD vector<uint8_t>& kmzData)
	{
		LOG_INFO("FlyManager: 发送【航线任务】命令事件 (从内存数据 {} 字节)...", kmzData.size());
		if (kmzData.empty())
		{
			LOG_ERROR("航线任务事件发送失败：KMZ 数据为空。");
			return;
		}
		plane::services::CommandQueue.enqueue(plane::services::CommandEvent::WaypointMission, kmzData);
	}

	void FlyManager::waypoint(_STD string_view kmzFilePath)
	{
		LOG_INFO("FlyManager: 正在处理【航线任务】(从文件: {})", kmzFilePath);

		std::filesystem::path path(kmzFilePath);
		if (!std::filesystem::exists(path))
		{
			LOG_ERROR("航线任务事件发送失败：KMZ 文件 '{}' 不存在。", kmzFilePath);
			return;
		}

		std::ifstream fileStream(path, std::ios::binary);
		if (!fileStream)
		{
			LOG_ERROR("航线任务事件发送失败：无法打开 KMZ 文件 '{}'。", kmzFilePath);
			return;
		}

		std::vector<uint8_t> kmzData { std::istreambuf_iterator<char>(fileStream), std::istreambuf_iterator<char>() };

		// 调用另一个重载来发送事件
		this->waypoint(kmzData);
	}

	void FlyManager::takeoff(const plane::protocol::TakeoffPayload& takeoffParams)
	{
		LOG_INFO("FlyManager: 发送【起飞】命令事件...");
		plane::services::CommandQueue.enqueue(plane::services::CommandEvent::Takeoff, takeoffParams);
	}

	void FlyManager::goHome(void)
	{
		LOG_INFO("FlyManager: 发送【返航】命令事件...");
		plane::services::CommandQueue.enqueue(plane::services::CommandEvent::GoHome, std::monostate {});
	}

	void FlyManager::hover(void)
	{
		LOG_INFO("FlyManager: 发送【悬停/中断】命令事件...");
		// “悬停”的业务意图是中断当前航线，所以我们发送 StopWaypointMission 事件
		plane::services::CommandQueue.enqueue(plane::services::CommandEvent::StopWaypointMission, std::monostate {});
	}

	void FlyManager::land(void)
	{
		LOG_INFO("FlyManager: 发送【降落】命令事件...");
		plane::services::CommandQueue.enqueue(plane::services::CommandEvent::Land, std::monostate {});
	}

	void FlyManager::setControlStrategy(int strategyCode)
	{
		LOG_INFO("FlyManager: 发送【设置云台控制策略】命令事件...");
		plane::services::CommandQueue.enqueue(plane::services::CommandEvent::SetControlStrategy, strategyCode);
	}

	void FlyManager::flyCircleAroundPoint(const plane::protocol::CircleFlyPayload& circleParams)
	{
		LOG_INFO("FlyManager: 发送【环绕飞行】命令事件...");
		plane::services::CommandQueue.enqueue(plane::services::CommandEvent::FlyCircleAroundPoint, circleParams);
	}

	void FlyManager::pauseWaypointMission()
	{
		LOG_INFO("FlyManager: 发送【暂停航线】命令事件...");
		plane::services::CommandQueue.enqueue(plane::services::CommandEvent::PauseWaypointMission, std::monostate {});
	}

	void FlyManager::resumeWaypointMission()
	{
		LOG_INFO("FlyManager: 发送【恢复航线】命令事件...");
		plane::services::CommandQueue.enqueue(plane::services::CommandEvent::ResumeWaypointMission, std::monostate {});
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

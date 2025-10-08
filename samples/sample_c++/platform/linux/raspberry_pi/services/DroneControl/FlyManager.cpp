// raspberry_pi/services/DroneControl/FlyManager.cpp

#include "FlyManager.h"

#include "services/PSDK/PSDKAdapter/PSDKAdapter.h"
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

	void FlyManager::waypoint(const _STD vector<_STD uint8_t>& kmzData)
	{
		LOG_INFO("FlyManager: 发送【航线任务】命令事件 (从内存数据 {} 字节)...", kmzData.size());
		if (kmzData.empty())
		{
			LOG_ERROR("航线任务事件发送失败：KMZ 数据为空。");
			return;
		}
		plane::services::EventManager::getInstance().publishCommand(plane::services::EventManager::CommandEvent::WaypointMission, kmzData);
	}

	void FlyManager::waypoint(_STD string_view kmzFilePath)
	{
		LOG_INFO("FlyManager: 正在处理【航线任务】(从文件: {})", kmzFilePath);

		_STD_FS path path(kmzFilePath);
		if (!_STD_FS exists(path))
		{
			LOG_ERROR("航线任务事件发送失败：KMZ 文件 '{}' 不存在。", kmzFilePath);
			return;
		}

		_STD ifstream fileStream(path, _STD ios::binary);
		if (!fileStream)
		{
			LOG_ERROR("航线任务事件发送失败：无法打开 KMZ 文件 '{}'。", kmzFilePath);
			return;
		}

		_STD vector<_STD uint8_t> kmzData { _STD istreambuf_iterator<char>(fileStream), _STD istreambuf_iterator<char>() };

		this->waypoint(kmzData);
	}

	void FlyManager::takeoff(const plane::protocol::TakeoffPayload& takeoffParams)
	{
		LOG_INFO("FlyManager: 发送【起飞】命令事件...");
		plane::services::EventManager::getInstance().publishCommand(plane::services::EventManager::CommandEvent::Takeoff, takeoffParams);
	}

	void FlyManager::goHome(void)
	{
		LOG_INFO("FlyManager: 发送【返航】命令事件...");
		plane::services::EventManager::getInstance().publishCommand(plane::services::EventManager::CommandEvent::GoHome, _STD monostate {});
	}

	void FlyManager::hover(void)
	{
		LOG_INFO("FlyManager: 发送【悬停/中断】命令事件...");
		plane::services::EventManager::getInstance().publishCommand(plane::services::EventManager::CommandEvent::StopWaypointMission,
																	_STD monostate {});
	}

	void FlyManager::land(void)
	{
		LOG_INFO("FlyManager: 发送【降落】命令事件...");
		plane::services::EventManager::getInstance().publishCommand(plane::services::EventManager::CommandEvent::Land, _STD monostate {});
	}

	void FlyManager::setControlStrategy(int strategyCode)
	{
		LOG_INFO("FlyManager: 发送【设置云台控制策略】命令事件...");
		plane::services::EventManager::getInstance().publishCommand(plane::services::EventManager::CommandEvent::SetControlStrategy,
																	strategyCode);
	}

	void FlyManager::flyCircleAroundPoint(const plane::protocol::CircleFlyPayload& circleParams)
	{
		LOG_INFO("FlyManager: 发送【环绕飞行】命令事件...");
		plane::services::EventManager::getInstance().publishCommand(plane::services::EventManager::CommandEvent::FlyCircleAroundPoint,
																	circleParams);
	}

	void FlyManager::pauseWaypointMission()
	{
		LOG_INFO("FlyManager: 发送【暂停航线】命令事件...");
		plane::services::EventManager::getInstance().publishCommand(plane::services::EventManager::CommandEvent::PauseWaypointMission,
																	_STD monostate {});
	}

	void FlyManager::resumeWaypointMission()
	{
		LOG_INFO("FlyManager: 发送【恢复航线】命令事件...");
		plane::services::EventManager::getInstance().publishCommand(plane::services::EventManager::CommandEvent::ResumeWaypointMission,
																	_STD monostate {});
	}

	void FlyManager::rotateGimbal(const plane::protocol::GimbalControlPayload& gimbalParams) const noexcept
	{
		LOG_INFO("FlyManager: 发送【云台角度控制】命令事件: 俯仰角={}, 偏航角={}", gimbalParams.FYJ, gimbalParams.PHJ);
		// 使用 GimbalControlPayload，MS=0 表示角度控制 (根据你的定义调整)
		plane::services::EventManager::getInstance().publishCommand(plane::services::EventManager::CommandEvent::RotateGimbal, gimbalParams);
	}

	void FlyManager::rotateGimbalBySpeed(const plane::protocol::GimbalControlPayload& gimbalParams) const noexcept
	{
		LOG_INFO("FlyManager: 发送【云台速度控制】命令事件: 俯仰角={}, 偏航角={}", gimbalParams.FYJ, gimbalParams.PHJ);
		// 使用 GimbalControlPayload，MS=1 表示速度控制 (根据你的定义调整)
		// rollSpeed 暂时没有对应字段，如果需要可以扩展 GimbalControlPayload
		plane::services::EventManager::getInstance().publishCommand(plane::services::EventManager::CommandEvent::RotateGimbalBySpeed,
																	gimbalParams);
	}

	void FlyManager::setCameraZoomFactor(const plane::protocol::ZoomControlPayload& zoomParams) const noexcept
	{
		LOG_INFO("FlyManager: 发送【相机变焦】命令事件...");
		plane::services::EventManager::getInstance().publishCommand(plane::services::EventManager::CommandEvent::SetCameraZoomFactor,
																	zoomParams);
	}

	void FlyManager::setCameraStreamSource(const plane::protocol::ZoomControlPayload& zoomParams) const noexcept
	{
		LOG_INFO("FlyManager: 发送【切换视频源】命令事件...");
		plane::services::EventManager::getInstance().publishCommand(plane::services::EventManager::CommandEvent::SetCameraStreamSource,
																	zoomParams);
	}

	void FlyManager::sendRawStickData(const plane::protocol::StickDataPayload& stickData) const noexcept
	{
		LOG_INFO("FlyManager: 发送【虚拟摇杆数据】命令事件...");
		plane::services::EventManager::getInstance().publishCommand(plane::services::EventManager::CommandEvent::SendRawStickData, stickData);
	}

	void FlyManager::enableVirtualStick(bool advancedMode) const noexcept
	{
		// YGMS: 0=关闭, 1=启用, 2=启用高级
		LOG_INFO("FlyManager: 发送【开启虚拟摇杆】命令事件, 高级模式: {}", advancedMode);
		plane::services::EventManager::getInstance().publishCommand(plane::services::EventManager::CommandEvent::EnableVirtualStick,
																	plane::protocol::StickModeSwitchPayload { (advancedMode ? 2 : 1) });
	}

	void FlyManager::disableVirtualStick(void) const noexcept
	{
		LOG_INFO("FlyManager: 发送【关闭虚拟摇杆】命令事件");
		plane::services::EventManager::getInstance().publishCommand(plane::services::EventManager::CommandEvent::DisableVirtualStick,
																	plane::protocol::StickModeSwitchPayload { 0 });
	}

	void FlyManager::sendNedVelocityCommand(const plane::protocol::NedVelocityPayload& velocityParams) const noexcept
	{
		LOG_INFO("FlyManager: 发送【NED 速度指令】命令事件...");
		plane::services::EventManager::getInstance().publishCommand(plane::services::EventManager::CommandEvent::SendNedVelocityCommand,
																	velocityParams);
	}
} // namespace plane::services

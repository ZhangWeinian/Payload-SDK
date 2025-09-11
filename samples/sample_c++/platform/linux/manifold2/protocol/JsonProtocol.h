#pragma once

#include "nlohmann/json.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace plane::protocol
{
	struct Waypoint
	{
		double				  longitude;   // JD: 经度
		double				  latitude;	   // WD: 纬度
		double				  altitude;	   // GD: 高度
		std::optional<double> speed;	   // SD: 速度 (可选)
		std::optional<double> gimbalPitch; // YTFYJ: 云台俯仰角 (可选)
	};

	struct WaypointPayload
	{
		std::vector<Waypoint> waypoints; // HDJ: 航点集
	};

	struct TakeoffPayload
	{
		double targetAltitude = 0.0; // MBGD: 目标高度
	};

	struct ControlStrategyPayload
	{
		int strategyCode = -1; // strategy: 控制策略代码
	};

	struct CircleFlyPayload
	{
		double longitude; // JD: 经度
		double latitude;  // WD: 纬度
		double altitude;  // GD: 高度
		double radius;	  // BJ: 半径
		double speed;	  // SD: 速度
		int	   laps;	  // QS: 圈数
	};

	struct GimbalControlPayload
	{
		double pitch;	 // FYJ: 俯仰角
		double yaw;		 // PHJ: 偏航角
		int	   mode = 1; // MS: 模式 (0:角度控制, 1:速度控制)
	};

	struct ZoomControlPayload
	{
		std::optional<double>	   zoomFactor;	 // BJB: 变焦倍数 (可选)
		std::optional<std::string> cameraSource; // XJLX: 相机源 (ir, wide, zoom) (可选)
	};

	struct StickDataPayload
	{
		int throttle = 1024; // YML: 油门量
		int yaw		 = 1024; // PHL: 偏航量
		int pitch	 = 1024; // FYL: 俯仰量
		int roll	 = 1024; // HGL: 横滚量
	};

	struct StickModeSwitchPayload
	{
		int stickMode = 0; // YGMS: 摇杆模式 (0:关闭, 1:启用, 2:启用高级)
	};

	struct NedVelocityPayload
	{
		double velocityNorth = 0.0; // SDN: 北向速度
		double velocityEast	 = 0.0; // SDD: 东向速度
		double velocityDown	 = 0.0; // SDX: 地向速度 (下为正)
		double yawRate		 = 0.0; // PHJ: 偏航角速率
	};

	// --- 状态上报 (SBZT) 的详细子结构 ---

	struct GpsInfo
	{
		int gpsSatelliteCount; // GPSSXSL: GPS卫星数量
		int lockLevel;		   // SFSL: 锁星水平
		int positionAccuracy;  // SXDW: 锁星定位
	};

	struct BatteryDetail
	{
		int remainingPercent; // DCSYSDL: 单电池剩余电量
		int voltage;		  // DY: 电压
	};

	struct BatteryInfo
	{
		int						   totalRemainingPercent; // SYDL: 总剩余电量
		int						   totalVoltage;		  // ZDY: 总电压
		std::vector<BatteryDetail> batteryDetails;		  // DCXXXX: 电池详细信息
	};

	struct VideoSource
	{
		std::string streamUrl; // SPURL: 视频流地址
		std::string protocol;  // SPXY: 视频协议
		int			status;	   // ZBZT: 直播状态
	};

	struct StatusPayload
	{
		GpsInfo					   gpsInfo;				  // SXZT:   搜星状态
		BatteryInfo				   batteryInfo;			  // DCXX:   电池信息
		double					   gimbalPitch;			  // YTFY:   云台俯仰
		double					   gimbalRoll;			  // YTHG:   云台横滚
		double					   gimbalYaw;			  // YTPH:   云台偏航
		double					   aircraftPitch;		  // FJFYJ:  飞机俯仰角
		double					   aircraftRoll;		  // FJHGJ:  飞机横滚角
		double					   aircraftYaw;			  // FJPHJ:  飞机偏航角
		double					   homeLatitude;		  // JWD:    Home点纬度
		double					   homeLongitude;		  // JJD:    Home点经度
		double					   homeAltitude;		  // JHB:    Home点海拔
		double					   takeoffAltitude;		  // JXDGD:  Home点相对高度
		double					   currentLatitude;		  // DQWD:   当前纬度
		double					   currentLongitude;	  // DQJD:   当前经度
		double					   relativeHeight;		  // XDQFGD: 相对起飞点高度
		double					   absoluteAltitude;	  // JDGD:   绝对高度 (海拔)
		double					   verticalSpeed;		  // CZSD:   垂直速度
		double					   horizontalSpeed;		  // SPSD:   水平速度
		double					   velocityEast;		  // VX:     东向速度
		double					   velocityNorth;		  // VY:     北向速度
		double					   velocityDown;		  // VZ:     地向速度 (下为正)
		int						   currentWaypointIndex;  // DQHD:   当前航点
		int						   totalWaypoints;		  // ZHD:    总航点
		double					   laserDistance;		  // JGCJ:   激光测距
		std::vector<VideoSource>   videoSources;		  // WZT:    视频源
		std::string				   manufacturer;		  // CJ:     厂家
		std::string				   model;				  // XH:     型号
		std::optional<std::string> flightMode;			  // MODE:   飞行模式 (可选)
		int						   isVirtualStickEnabled; // VSE:    虚拟摇杆启用
		int						   isAdvancedModeEnabled; // AME:    高级模式启用
	};

	// --- 其他上报消息的 Payload ---

	struct MissionInfoPayload
	{
		std::string planeSerialNumber; // FJSN: 飞机序列号
		std::string controllerIp;	   // YKQIP: 遥控器IP
		std::string rtspUrl;		   // YSRTSP: RTSP地址
	};

	struct HealthAlertPayload
	{
		int			level;		 // GJDJ: 告警等级
		int			module;		 // GJMK: 告警模块
		std::string messageCode; // GJM: 告警码
		std::string title;		 // GJBT: 告警标题
		std::string description; // GJMS: 告警描述
	};

	struct HealthStatusPayload
	{
		std::vector<HealthAlertPayload> alerts; // GJLB: 告警列表
	};

	template<typename T>
	struct NetworkMessage
	{
		std::string planeCode;			// ZBID: 飞机编码
		std::string messageId;			// XXID: 消息ID
		std::string messageType;		// XXLX: 消息类型
		int64_t		timestamp;			// SJC: 时间戳 (毫秒)
		std::string formattedTimestamp; // SBSJ: 格式化时间戳
		T			payload;			// XXXX: 消息载荷
	};

	// 上报数据: C++ -> JSON
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(GpsInfo, gpsSatelliteCount, lockLevel, positionAccuracy);
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(BatteryDetail, remainingPercent, voltage);
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(BatteryInfo, totalRemainingPercent, totalVoltage, batteryDetails);
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(VideoSource, streamUrl, protocol, status);
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(StatusPayload,
									   gpsInfo,
									   batteryInfo,
									   gimbalPitch,
									   gimbalRoll,
									   gimbalYaw,
									   aircraftPitch,
									   aircraftRoll,
									   aircraftYaw,
									   homeLatitude,
									   homeLongitude,
									   homeAltitude,
									   takeoffAltitude,
									   currentLatitude,
									   currentLongitude,
									   relativeHeight,
									   absoluteAltitude,
									   verticalSpeed,
									   horizontalSpeed,
									   velocityEast,
									   velocityNorth,
									   velocityDown,
									   currentWaypointIndex,
									   totalWaypoints,
									   laserDistance,
									   videoSources,
									   manufacturer,
									   model,
									   flightMode,
									   isVirtualStickEnabled,
									   isAdvancedModeEnabled);
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(MissionInfoPayload, planeSerialNumber, controllerIp, rtspUrl);
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(HealthAlertPayload, level, module, messageCode, title, description);
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(HealthStatusPayload, alerts);

	// 下行指令: JSON -> C++
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Waypoint, longitude, latitude, altitude, speed, gimbalPitch);
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(WaypointPayload, waypoints);
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(TakeoffPayload, targetAltitude);
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ControlStrategyPayload, strategyCode);
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CircleFlyPayload, longitude, latitude, altitude, radius, speed, laps);
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(GimbalControlPayload, pitch, yaw, mode);
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ZoomControlPayload, zoomFactor, cameraSource);
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(StickDataPayload, throttle, yaw, pitch, roll);
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(StickModeSwitchPayload, stickMode);
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(NedVelocityPayload, velocityNorth, velocityEast, velocityDown, yawRate);
} // namespace plane::protocol

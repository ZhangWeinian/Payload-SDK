// cy_psdk/protocol/HeartbeatDataClass.h

#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "define.h"

namespace plane::protocol
{
	struct GpsInfo
	{
		int GPSSXSL {}; // GPS 卫星数量
		int SFSL {};	// 锁星水平
		int SXDW {};	// 锁星定位
	};

	struct BatteryDetail
	{
		int DCSYSDL {}; // 单电池剩余电量
		int DY {};		// 电压
	};

	struct BatteryInfo
	{
		int	 SYDL {};						  // 总剩余电量
		int	 ZDY {};						  // 总电压
		_STD vector<BatteryDetail> DCXXXX {}; // 电池详细信息
	};

	struct VideoSource
	{
		_STD string SPURL {}; // 视频流地址
		_STD string SPXY {};  // 视频协议
		int			ZBZT {};  // 直播状态
	};

	struct StatusPayload
	{
		GpsInfo		SXZT {};				// 搜星状态
		BatteryInfo DCXX {};				// 电池信息
		double		YTFY {};				// 云台俯仰
		double		YTHG {};				// 云台横滚
		double		YTPH {};				// 云台偏航
		double		FJFYJ {};				// 飞机俯仰角
		double		FJHGJ {};				// 飞机横滚角
		double		FJPHJ {};				// 飞机偏航角
		double		JJD {};					// Home 点经度
		double		JWD {};					// Home 点纬度
		double		JHB {};					// Home 点海拔
		double		JXDGD {};				// Home 点相对高度
		double		DQWD {};				// 当前纬度
		double		DQJD {};				// 当前经度
		double		XDQFGD {};				// 相对起飞点高度
		double		JDGD {};				// 绝对高度 (海拔)
		double		CZSD {};				// 垂直速度
		double		SPSD {};				// 水平速度
		double		VX {};					// 东向速度
		double		VY {};					// 北向速度
		double		VZ {};					// 地向速度 (下为正)
		int			DQHD {};				// 当前航点
		int			ZHD {};					// 总航点
		double		JGCJ {};				// 激光测距
		_STD vector<VideoSource> WZT {};	// 视频源
		_STD string				 CJ {};		// 厂家
		_STD string				 XH {};		// 型号
		_STD optional<_STD string> MODE {}; // 飞行模式 (可选)
		int						   VSE {};	// 虚拟摇杆启用
		int						   AME {};	// 高级模式启用
	};

	struct MissionInfoPayload
	{
		_STD string FJSN {};   // PlaneCode
		_STD string YKQIP {};  // 遥控器 IP
		_STD string YSRTSP {}; // RTSP 地址
	};

	struct HealthAlertPayload
	{
		int			GJDJ {}; // 告警等级
		int			GJMK {}; // 告警模块
		_STD string GJM {};	 // 告警码
		_STD string GJBT {}; // 告警标题
		_STD string GJMS {}; // 告警描述
	};

	struct HealthStatusPayload
	{
		_STD vector<HealthAlertPayload> GJLB {}; // 告警列表
	};

	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(GpsInfo, GPSSXSL, SFSL, SXDW);
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(BatteryDetail, DCSYSDL, DY);
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(BatteryInfo, SYDL, ZDY, DCXXXX);
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(VideoSource, SPURL, SPXY, ZBZT);
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(StatusPayload,
													SXZT,
													DCXX,
													YTFY,
													YTHG,
													YTPH,
													FJFYJ,
													FJHGJ,
													FJPHJ,
													JJD,
													JWD,
													JHB,
													JXDGD,
													DQWD,
													DQJD,
													XDQFGD,
													JDGD,
													CZSD,
													SPSD,
													VX,
													VY,
													VZ,
													DQHD,
													ZHD,
													JGCJ,
													WZT,
													CJ,
													XH,
													MODE,
													VSE,
													AME);
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(MissionInfoPayload, FJSN, YKQIP, YSRTSP);
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(HealthAlertPayload, GJDJ, GJMK, GJM, GJBT, GJMS);
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(HealthStatusPayload, GJLB);
} // namespace plane::protocol

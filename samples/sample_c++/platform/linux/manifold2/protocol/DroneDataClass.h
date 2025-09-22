#pragma once

#include "nlohmann/json.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace plane::protocol
{
	using n_json = nlohmann::json;

	enum class MissionExecutionStatus : int
	{
		EXECUTING		 = 1,
		PAUSED			 = 2,
		FINISHED		 = 3,
		UPLOADING		 = 4,
		UPLOAD_COMPLETE	 = 5,
		UPLOAD_FAILED	 = 6,
		EXECUTION_FAILED = 7
	};

	enum class MissionControlAction
	{
		START,
		PAUSE,
		RESUME,
		STOP
	};

	inline void to_json(n_json& j, const MissionControlAction& action)
	{
		switch (action)
		{
			case MissionControlAction::START:
			{
				j = "RWKS";
				break;
			}
			case MissionControlAction::PAUSE:
			{
				j = "RWZT";
				break;
			}
			case MissionControlAction::RESUME:
			{
				j = "RWJX";
				break;
			}
			case MissionControlAction::STOP:
			{
				j = "RWJS";
				break;
			}
			default:
			{
				j = nullptr;
				break;
			}
		}
	}

	inline void from_json(const n_json& j, MissionControlAction& action)
	{
		const std::string s { j.get<std::string>() };
		if (s == "RWKS")
		{
			action = MissionControlAction::START;
		}
		else if (s == "RWZT")
		{
			action = MissionControlAction::PAUSE;
		}
		else if (s == "RWJX")
		{
			action = MissionControlAction::RESUME;
		}
		else if (s == "RWJS")
		{
			action = MissionControlAction::STOP;
		}
		// 在实际业务逻辑中可能需要处理未知字符串的情况
	}

	enum class RthMode : int
	{
		INTELLIGENT		= 0,
		PRESET_ALTITUDE = 1
	};

	enum class RcLostAction : int
	{
		HOVER	= 0,
		LAND	= 1,
		GO_HOME = 2
	};

	enum class CommanderModeLostAction : int
	{
		CONTINUE_MISSION	= 0,
		EXECUTE_LOST_ACTION = 1
	};

	enum class CommanderFlightMode : int
	{
		INTELLIGENT		= 0,
		PRESET_ALTITUDE = 1
	};

	enum class TargetType
	{
		INDEX,
		COORDINATE,
		RECTANGLE
	};

	inline void to_json(n_json& j, const TargetType& type)
	{
		switch (type)
		{
			case TargetType::INDEX:
			{
				j = "MBSY";
				break;
			}
			case TargetType::COORDINATE:
			{
				j = "MBWZ";
				break;
			}
			case TargetType::RECTANGLE:
			{
				j = "MBJX";
				break;
			}
			default:
			{
				j = nullptr;
				break;
			}
		}
	}

	inline void from_json(const n_json& j, TargetType& type)
	{
		const std::string s { j.get<std::string>() };
		if (s == "MBSY")
		{
			type = TargetType::INDEX;
		}
		else if (s == "MBWZ")
		{
			type = TargetType::COORDINATE;
		}
		else if (s == "MBJX")
		{
			type = TargetType::RECTANGLE;
		}
	}

	struct WaypointAction
	{
		int LX; // 类型
		int CS; // 参数
	};

	struct Waypoint
	{
		double									   JD {};			// 经度
		double									   WD {};			// 维度
		double									   GD {};			// 高度
		double									   SD { 0.0 };		// 速度
		std::optional<double>					   YTFYJ { -90.0 }; // 云台俯仰角
		std::optional<double>					   PHJ {};			// 偏航角
		std::optional<bool>						   SFTY {};			// 是否飞越
		std::optional<std::vector<WaypointAction>> DZJ {};			// 动作集
	};

	struct WaypointPayload
	{
		std::optional<std::string> RWID {}; // 任务 ID
		std::vector<Waypoint>	   HDJ {};	// 航点集
	};

	struct MissionProgressPayload
	{
		std::optional<std::string> RWID {}; // 任务 ID
		std::optional<int>		   DQHD {}; // 当前航点
		std::optional<int>		   ZHD {};	// 总航点
		std::optional<int>		   JD {};	// 进度
		std::optional<int>		   ZT {};	// 状态
	};

	struct MissionControlPayload
	{
		std::string RWID {}; // RWID
		std::string RWDZ {}; // RWDZ
	};

	struct LandingPayload
	{
		double JD {}; // JD
		double WD {}; // WD
	};

	struct TakeoffPayload
	{
		std::optional<double> MBWD {};	// 目标纬度
		std::optional<double> MBJD {};	// 目标经度
		std::optional<double> MBGD {};	// 目标高度
		std::optional<int>	  FHMS {};	// 返航模式
		std::optional<int>	  FHGD {};	// 返航高度
		std::optional<int>	  ZDMSD {}; // 最大速度
		std::optional<int>	  AQJC {};	// 安全预检
	};

	struct FlyToPoint
	{
		double JD {}; // 经度
		double WD {}; // 维度
		double GD {}; // 高度
	};

	struct FlyToPayload
	{
		std::optional<std::string> FXMBID {}; // 目标点 ID（年+月日+序号）
		std::optional<double>	   ZDMSD {};  // 最大速度
		std::vector<FlyToPoint>	   MBDS {};	  // 目标点坐标集合
	};

	struct UpdateFlyToPayload
	{
		std::optional<double>	ZDMSD;	// 飞行最大速度限制
		std::vector<FlyToPoint> GXMBDS; // 需要更新的航点坐标集合
	};

	struct TargetCoordinate
	{
		double JD {}; // 经度
		double WD {}; // 纬度
		double GD {}; // 高度
	};

	struct TargetRectangle
	{
		int X {}; // X
		int Y {}; // Y
		int W {}; // W
		int H {}; // H
	};

	struct ControlStrategyPayload
	{
		std::optional<int> YTJSCL {}; // 云台转身策略
	};

	struct SmartFollowPayload
	{
		std::string						MBLX {}; // 目标类型
		std::optional<TargetCoordinate> MBWZ {}; // 目标位置
		std::optional<int>				MBSY {}; // 目标索引
		std::optional<TargetRectangle>	MBJX {}; // 目标矩形
	};

	struct CircleFlyPayload
	{
		double JD {}; // 经度
		double WD {}; // 纬度
		double GD {}; // 高度
		double SD {}; // 速度
		double BJ {}; // 半径
		int	   QS {}; // 圈数
	};

	struct GimbalControlPayload
	{
		double FYJ {};	 // 俯仰角
		double PHJ {};	 // 偏航角
		int	   MS { 1 }; // 模式 (0:角度控制, 1:速度控制)
	};

	struct ZoomControlPayload
	{
		std::optional<std::string> XJSY {}; // 相机索引
		std::optional<std::string> XJLX {}; // 相机类型
		std::optional<double>	   BJB {};	// 变焦倍数
	};

	struct StickDataPayload
	{
		int YML { 1024 }; // 油门量
		int PHL { 1024 }; // 偏航量
		int FYL { 1024 }; // 俯仰量
		int HGL { 1024 }; // 横滚量
	};

	struct StickModeSwitchPayload
	{
		int YGMS { 0 }; // 摇杆模式 (0:关闭, 1:启用, 2:启用高级)
	};

	struct NedVelocityPayload
	{
		double SDN { 0.0 }; // 北向速度
		double SDD { 0.0 }; // 东向速度
		double SDX { 0.0 }; // 地向速度 (下为正)
		double PHJ { 0.0 }; // 偏航角速率
	};

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
		int						   SYDL {};	  // 总剩余电量
		int						   ZDY {};	  // 总电压
		std::vector<BatteryDetail> DCXXXX {}; // 电池详细信息
	};

	struct VideoSource
	{
		std::string SPURL {}; // 视频流地址
		std::string SPXY {};  // 视频协议
		int			ZBZT {};  // 直播状态
	};

	struct StatusPayload
	{
		GpsInfo					   SXZT {};	  // 搜星状态
		BatteryInfo				   DCXX {};	  // 电池信息
		double					   YTFY {};	  // 云台俯仰
		double					   YTHG {};	  // 云台横滚
		double					   YTPH {};	  // 云台偏航
		double					   FJFYJ {};  // 飞机俯仰角
		double					   FJHGJ {};  // 飞机横滚角
		double					   FJPHJ {};  // 飞机偏航角
		double					   JJD {};	  // Home 点经度
		double					   JWD {};	  // Home 点纬度
		double					   JHB {};	  // Home 点海拔
		double					   JXDGD {};  // Home 点相对高度
		double					   DQWD {};	  // 当前纬度
		double					   DQJD {};	  // 当前经度
		double					   XDQFGD {}; // 相对起飞点高度
		double					   JDGD {};	  // 绝对高度 (海拔)
		double					   CZSD {};	  // 垂直速度
		double					   SPSD {};	  // 水平速度
		double					   VX {};	  // 东向速度
		double					   VY {};	  // 北向速度
		double					   VZ {};	  // 地向速度 (下为正)
		int						   DQHD {};	  // 当前航点
		int						   ZHD {};	  // 总航点
		double					   JGCJ {};	  // 激光测距
		std::vector<VideoSource>   WZT {};	  // 视频源
		std::string				   CJ {};	  // 厂家
		std::string				   XH {};	  // 型号
		std::optional<std::string> MODE {};	  // 飞行模式 (可选)
		int						   VSE {};	  // 虚拟摇杆启用
		int						   AME {};	  // 高级模式启用
	};

	struct MissionInfoPayload
	{
		std::string FJSN {};   // PlaneCode
		std::string YKQIP {};  // 遥控器 IP
		std::string YSRTSP {}; // RTSP 地址
	};

	struct HealthAlertPayload
	{
		int			GJDJ {}; // 告警等级
		int			GJMK {}; // 告警模块
		std::string GJM {};	 // 告警码
		std::string GJBT {}; // 告警标题
		std::string GJMS {}; // 告警描述
	};

	struct HealthStatusPayload
	{
		std::vector<HealthAlertPayload> GJLB {}; // 告警列表
	};

	template<typename T>
	struct NetworkMessage
	{
		std::string				   ZBID {}; // 装备 ID
		std::string				   XXID {}; // 消息 ID
		std::string				   XXLX {}; // 消息类型
		int64_t					   SJC {};	// 时间戳 (毫秒)
		std::optional<std::string> SBSJ {}; // 上报时间
		std::optional<T>		   XXXX {}; // 消息信息
	};

	// 上报数据: C++ -> JSON
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

	// 下行指令: JSON -> C++
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Waypoint, JD, WD, GD, SD, YTFYJ, PHJ, SFTY, DZJ);
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(WaypointPayload, HDJ, RWID);
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(TakeoffPayload, MBWD, MBJD, MBGD, FHMS, FHGD, ZDMSD, AQJC);
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ControlStrategyPayload, YTJSCL);
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CircleFlyPayload, JD, WD, GD, SD, BJ, QS);
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(GimbalControlPayload, FYJ, PHJ, MS);
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ZoomControlPayload, XJSY, XJLX, BJB);
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(WaypointAction, LX, CS);
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(StickDataPayload, YML, PHL, FYL, HGL);
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(StickModeSwitchPayload, YGMS);
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(NedVelocityPayload, SDN, SDD, SDX, PHJ);
} // namespace plane::protocol

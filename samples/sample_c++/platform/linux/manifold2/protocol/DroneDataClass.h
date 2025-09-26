// manifold2/protocol/DroneDataClass.h

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "define.h"

namespace plane::protocol
{
	using n_json = _NLOHMANN_JSON json;

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
		const _STD string s { j.get<_STD string>() };
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
		const _STD string s { j.get<_STD string>() };
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
		int LX {}; // 类型
		int CS {}; // 参数
	};

	struct Waypoint
	{
		double JD {};									   // 经度
		double WD {};									   // 维度
		double GD {};									   // 高度
		double SD {};									   // 速度
		_STD optional<double> YTFYJ { -90 };			   // 云台俯仰角
		_STD optional<double> PHJ {};					   // 偏航角
		_STD optional<bool> SFTY {};					   // 是否飞越
		_STD optional<_STD vector<WaypointAction>> DZJ {}; // 动作集
	};

	struct WaypointPayload
	{
		_STD optional<_STD string> RWID {}; // 任务 ID
		_STD vector<Waypoint> HDJ {};		// 航点集
	};

	struct MissionProgressPayload
	{
		_STD optional<_STD string> RWID {}; // 任务 ID
		_STD optional<int> DQHD {};			// 当前航点
		_STD optional<int> ZHD {};			// 总航点
		_STD optional<int> JD {};			// 进度
		_STD optional<int> ZT {};			// 状态
	};

	struct MissionControlPayload
	{
		_STD string RWID {}; // RWID
		_STD string RWDZ {}; // RWDZ
	};

	struct LandingPayload
	{
		double JD {}; // JD
		double WD {}; // WD
	};

	struct TakeoffPayload
	{
		_STD optional<double> MBWD {}; // 目标纬度
		_STD optional<double> MBJD {}; // 目标经度
		_STD optional<double> MBGD {}; // 目标高度
		_STD optional<int> FHMS {};	   // 返航模式
		_STD optional<int> FHGD {};	   // 返航高度
		_STD optional<int> ZDMSD {};   // 最大速度
		_STD optional<int> AQJC {};	   // 安全预检
	};

	struct FlyToPoint
	{
		double JD {}; // 经度
		double WD {}; // 维度
		double GD {}; // 高度
	};

	struct FlyToPayload
	{
		_STD optional<_STD string> FXMBID {}; // 目标点 ID（年+月日+序号）
		_STD optional<double> ZDMSD {};		  // 最大速度
		_STD vector<FlyToPoint> MBDS {};	  // 目标点坐标集合
	};

	struct UpdateFlyToPayload
	{
		_STD optional<double> ZDMSD;	// 飞行最大速度限制
		_STD vector<FlyToPoint> GXMBDS; // 需要更新的航点坐标集合
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
		_STD optional<int> YTJSCL {}; // 云台转身策略
	};

	struct SmartFollowPayload
	{
		_STD string MBLX {};					 // 目标类型
		_STD optional<TargetCoordinate> MBWZ {}; // 目标位置
		_STD optional<int> MBSY {};				 // 目标索引
		_STD optional<TargetRectangle> MBJX {};	 // 目标矩形
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
		_STD optional<_STD string> XJSY {}; // 相机索引
		_STD optional<_STD string> XJLX {}; // 相机类型
		_STD optional<double> BJB {};		// 变焦倍数
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
		double SDN { 0 }; // 北向速度
		double SDD { 0 }; // 东向速度
		double SDX { 0 }; // 地向速度 (下为正)
		double PHJ { 0 }; // 偏航角速率
	};

	template<typename T>
	struct NetworkMessage
	{
		_STD string ZBID {};				// 装备 ID
		_STD string XXID {};				// 消息 ID
		_STD string XXLX {};				// 消息类型
		int64_t		SJC {};					// 时间戳 (毫秒)
		_STD optional<_STD string> SBSJ {}; // 上报时间
		_STD optional<T> XXXX {};			// 消息信息
	};

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

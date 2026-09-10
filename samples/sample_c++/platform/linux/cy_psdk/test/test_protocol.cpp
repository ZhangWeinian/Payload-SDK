// cy_psdk/tests/test_protocol.cpp
//
// 协议数据类纯逻辑测试: JSON 序列化往返、缺字段默认值、枚举字符串、NetworkMessage 信封。

#include "protocol/DroneDataClass.h"
#include "protocol/HeartbeatDataClass.h"

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <string>

namespace
{
	using plane::protocol::n_json;
	using namespace plane::protocol;
} // namespace

TEST(ProtocolDataClass, MissionControlActionEnumRoundTrip)
{
	n_json j = MissionControlAction::RWJS;
	EXPECT_EQ(j.get<_STD string>(), "RWJS");
	EXPECT_EQ(j.get<MissionControlAction>(), MissionControlAction::RWJS);

	n_json j2 = MissionControlAction::RWKS;
	EXPECT_EQ(j2.get<_STD string>(), "RWKS");
	EXPECT_EQ(j2.get<MissionControlAction>(), MissionControlAction::RWKS);
}

TEST(ProtocolDataClass, WaypointDefaultsForMissingFields)
{
	const auto parsed = n_json::parse(R"({"JD":1.5,"WD":2.5,"GD":10.0})").get<Waypoint>();
	EXPECT_DOUBLE_EQ(parsed.JD, 1.5);
	EXPECT_DOUBLE_EQ(parsed.WD, 2.5);
	EXPECT_DOUBLE_EQ(parsed.GD, 10.0);
	EXPECT_DOUBLE_EQ(parsed.SD, 0.0);	   // 未提供 -> 默认 0
	EXPECT_TRUE(parsed.YTFYJ.has_value()); // NSDMI 默认 {-90} -> 有值
	EXPECT_DOUBLE_EQ(parsed.YTFYJ.value(), -90.0);
	EXPECT_FALSE(parsed.PHJ.has_value());  // optional 无 NSDMI -> 缺字段为无值
}

TEST(ProtocolDataClass, WaypointFullRoundTrip)
{
	Waypoint w {};
	w.JD	= 116.3912;
	w.WD	= 39.9075;
	w.GD	= 120.0;
	w.SD	= 6.5;
	w.YTFYJ = -30.0;
	w.DZJ	= _STD vector<WaypointAction> {
		WaypointAction { .LX = 1, .CS = 42 }
	};

	n_json	   j	= w;
	const auto back = j.get<Waypoint>();

	EXPECT_DOUBLE_EQ(back.JD, 116.3912);
	EXPECT_DOUBLE_EQ(back.YTFYJ.value(), -30.0);
	ASSERT_TRUE(back.DZJ.has_value());
	ASSERT_EQ(back.DZJ->size(), 1u);
	EXPECT_EQ(back.DZJ->front().CS, 42);
}

TEST(ProtocolDataClass, NetworkMessageOmitsEmptyOptionalPayload)
{
	NetworkMessage<MissionProgressPayload> msg {};
	msg.ZBID = "10074000";
	msg.XXID = "RWJD-10074000-1";
	msg.XXLX = "RWJD";
	msg.SJC	 = 123'456'789;

	n_json j = msg;
	EXPECT_FALSE(j.contains("XXXX"));
	EXPECT_FALSE(j.contains("SBSJ"));
}

TEST(ProtocolDataClass, NetworkMessageRoundTripWithPayload)
{
	NetworkMessage<MissionProgressPayload> msg {};
	msg.ZBID = "10074000";
	msg.XXID = "RWJD-10074000-1";
	msg.XXLX = "RWJD";
	msg.SJC	 = 123'456'789;
	msg.SBSJ = "2026-09-09 10:00:00";
	msg.XXXX = MissionProgressPayload { .RWID = "task-1", .DQHD = 3, .ZHD = 10, .JD = 30, .ZT = 48 };

	n_json j = msg;
	EXPECT_TRUE(j.contains("XXXX"));
	EXPECT_EQ(j.at("SBSJ").get<_STD string>(), "2026-09-09 10:00:00");

	const auto back = j.get<NetworkMessage<MissionProgressPayload>>();
	ASSERT_TRUE(back.XXXX.has_value());
	EXPECT_EQ(back.XXXX->RWID.value(), "task-1");
	EXPECT_EQ(back.XXXX->ZT.value(), 48);
	EXPECT_EQ(back.SJC, 123'456'789);
}

TEST(ProtocolDataClass, StatusPayloadFieldRoundTrip)
{
	StatusPayload st {};
	st.DQJD			= 116.3912;
	st.DQWD			= 39.9075;
	st.YTFY			= -12.5;
	st.CJ			= "DJI";
	st.XH			= "M350";
	st.MODE			= "P-GPS";

	n_json	   j	= st;
	const auto back = j.get<StatusPayload>();

	EXPECT_DOUBLE_EQ(back.DQJD, 116.3912);
	EXPECT_DOUBLE_EQ(back.YTFY, -12.5);
	EXPECT_EQ(back.CJ, "DJI");
	EXPECT_EQ(back.MODE, "P-GPS");
	EXPECT_TRUE(back.WZT.empty()); // 未提供 -> 默认空
}

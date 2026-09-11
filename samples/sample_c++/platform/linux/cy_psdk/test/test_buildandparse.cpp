// cy_psdk/tests/test_buildandparse.cpp
//
// 覆盖 JsonConverter: 上行 JSON 信封构建与 MQTT 消息解析路由。
// 依赖已加载的 ConfigManager (测试夹具 plane.code=0A1B2C3D4E5F6078, 见 test_config.cpp / test_config_helpers.h)。

#include "config/ConfigManager.h"
#include "manager/mqtt/handler/MessageHandler.h"
#include "protocol/DroneDataClass.h"
#include "protocol/HeartbeatDataClass.h"
#include "test_config_helpers.h"
#include "utils/json_converter/BuildAndParse.h"

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <regex>
#include <string>

namespace
{
	using JsonConverter = plane::utils::JsonConverter;
	using plane::protocol::n_json;

	void ensureConfigLoaded()
	{
		// 幂等: 若已被其他 TU 加载(内容相同)则直接返回 true
		plane::test::writeSharedConfig();
		plane::config::ConfigManager::getInstance().loadAndCheck(plane::test::sharedConfigPath());
	}
} // namespace

TEST(JsonConverter, BuildStatusReportEnvelope)
{
	ensureConfigLoaded();

	plane::protocol::StatusPayload payload {};
	payload.DQJD = 116.3912;
	payload.DQWD = 39.9075;
	payload.YTFY = -12.5;
	payload.CJ	 = "DJI";

	const auto json_string { JsonConverter::buildStatusReportJson(payload) };
	const auto parsed = n_json::parse(json_string);

	EXPECT_EQ(parsed.at("ZBID").get<_STD string>(), "0A1B2C3D4E5F6078");
	EXPECT_EQ(parsed.at("XXLX").get<_STD string>(), "SBZT");
	EXPECT_GT(parsed.at("SJC").get<int64_t>(), 0);
	EXPECT_EQ(parsed.at("XXID").get<_STD string>().substr(0, 4), "SBZT");
	EXPECT_TRUE(_STD regex_match(parsed.at("SBSJ").get<_STD string>(), _STD regex { R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})" }));

	const auto& inner { parsed.at("XXXX") };
	EXPECT_DOUBLE_EQ(inner.at("DQJD").get<double>(), 116.3912);
	EXPECT_DOUBLE_EQ(inner.at("DQWD").get<double>(), 39.9075);
	EXPECT_DOUBLE_EQ(inner.at("YTFY").get<double>(), -12.5);
	EXPECT_EQ(inner.at("CJ").get<_STD string>(), "DJI");
}

TEST(JsonConverter, BuildMissionInfoEnvelope)
{
	ensureConfigLoaded();

	plane::protocol::MissionInfoPayload payload {};
	payload.FJSN	  = "0A1B2C3D4E5F6078";
	payload.YKQIP	  = "192.168.1.10";
	payload.YSRTSP	  = "rtsp://192.168.1.10:8554/live/1";

	const auto parsed = n_json::parse(JsonConverter::buildMissionInfoJson(payload));

	EXPECT_EQ(parsed.at("XXLX").get<_STD string>(), "GDXX");
	EXPECT_EQ(parsed.at("XXXX").at("FJSN").get<_STD string>(), "0A1B2C3D4E5F6078");
	EXPECT_EQ(parsed.at("XXXX").at("YKQIP").get<_STD string>(), "192.168.1.10");
}

TEST(JsonConverter, BuildHealthStatusEnvelopeWithEmptyAlerts)
{
	ensureConfigLoaded();

	plane::protocol::HealthStatusPayload payload {};
	plane::protocol::HealthAlertPayload	 alert {};
	alert.GJDJ = 1;
	alert.GJM  = "123456";
	payload.GJLB.push_back(alert);

	const auto parsed = n_json::parse(JsonConverter::buildHealthStatusJson(payload));

	EXPECT_EQ(parsed.at("XXLX").get<_STD string>(), "JKGL");
	ASSERT_TRUE(parsed.at("XXXX").contains("GJLB"));
	EXPECT_EQ(parsed.at("XXXX").at("GJLB").size(), 1);
	EXPECT_EQ(parsed.at("XXXX").at("GJLB")[0].at("GJM").get<_STD string>(), "123456");
}

TEST(JsonConverter, ParseAndRouteDispatchesMatchingPlane)
{
	ensureConfigLoaded();

	int	 call_count { 0 };
	auto captured { n_json::object() };

	plane::manager::MqttMessageHandler::getInstance().registerHandler(
		"/unit/jsonc/route",
		"SBZT",
		[&](const n_json& payload)
		{
			++call_count;
			captured = payload;
		}
	);

	const _STD string message { R"({"ZBID":"0A1B2C3D4E5F6078","XXLX":"SBZT","XXXX":{"DQJD":116.39,"YTFY":-5.0}})" };
	JsonConverter::parseAndRouteMessage("/unit/jsonc/route", message);

	EXPECT_EQ(call_count, 1);
	EXPECT_DOUBLE_EQ(captured.at("DQJD").get<double>(), 116.39);
}

TEST(JsonConverter, ParseAndRouteIgnoresOtherPlane)
{
	ensureConfigLoaded();

	int call_count { 0 };
	plane::manager::MqttMessageHandler::getInstance().registerHandler(
		"/unit/jsonc/other",
		"SBZT",
		[&](const n_json&)
		{
			++call_count;
		}
	);

	const _STD string message { R"({"ZBID":"99999999","XXLX":"SBZT","XXXX":{"DQJD":116.39}})" };
	JsonConverter::parseAndRouteMessage("/unit/jsonc/other", message);

	EXPECT_EQ(call_count, 0);
}

TEST(JsonConverter, ParseAndRouteRejectsMessageWithoutZbid)
{
	ensureConfigLoaded();

	int call_count { 0 };
	plane::manager::MqttMessageHandler::getInstance().registerHandler(
		"/unit/jsonc/nozbid",
		"SBZT",
		[&](const n_json&)
		{
			++call_count;
		}
	);

	// 缺少 ZBID 时直接忽略且不抛异常
	EXPECT_NO_THROW(JsonConverter::parseAndRouteMessage("/unit/jsonc/nozbid", R"({"XXLX":"SBZT"})"));
	EXPECT_EQ(call_count, 0);
}

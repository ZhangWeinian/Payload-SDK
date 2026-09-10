// cy_psdk/test/test_ws_client.cpp
//
// WsClient 协议纯函数单测 (不涉及网络):
//   - buildWsUrl: 对齐 msdk webSocketUrl 派生规则 "ws://<ip>:8888"
//   - buildSubscribePayload: 对齐 msdk 订阅报文 {"cmd":"subscribe","types":[...]}

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

#include "manager/websocket/WsClient.h"

namespace
{
	TEST(WsClientTest, BuildWsUrlMatchesMsdkDerivation)
	{
		EXPECT_EQ(plane::manager::WsClient::buildWsUrl("192.168.1.10", 8888), "ws://192.168.1.10:8888");
	}

	TEST(WsClientTest, BuildWsUrlEmptyIpYieldsEmpty)
	{
		// msdk: registryIp 为空 -> webSocketUrl 为空 -> 不发起连接
		EXPECT_TRUE(plane::manager::WsClient::buildWsUrl("", 8888).empty());
	}

	TEST(WsClientTest, BuildSubscribePayloadMatchesMsdkCommand)
	{
		const _STD vector<_STD string> types { "stationSwarmState", "event", "stationTaskStatus" };
		const _STD string			   payload { plane::manager::WsClient::buildSubscribePayload(types) };

		const _NLOHMANN_JSON json parsed = _NLOHMANN_JSON json::parse(payload);
		ASSERT_TRUE(parsed.is_object());
		EXPECT_EQ(parsed["cmd"].get<_STD string>(), "subscribe");
		ASSERT_TRUE(parsed["types"].is_array());
		EXPECT_EQ(parsed["types"].size(), 3u);
		EXPECT_EQ(parsed["types"][0].get<_STD string>(), "stationSwarmState");
		EXPECT_EQ(parsed["types"][1].get<_STD string>(), "event");
		EXPECT_EQ(parsed["types"][2].get<_STD string>(), "stationTaskStatus");
	}

	TEST(WsClientTest, SubscribePayloadTypesIsArray)
	{
		// 防回归: types 必须是 JSON 数组 (避免 nlohmann 花括号初始化陷阱)
		const _STD vector<_STD string> empty {};
		const _STD string			   payload { plane::manager::WsClient::buildSubscribePayload(empty) };

		const _NLOHMANN_JSON json parsed = _NLOHMANN_JSON json::parse(payload);
		ASSERT_TRUE(parsed.is_object());
		EXPECT_TRUE(parsed.contains("types"));
		EXPECT_TRUE(parsed["types"].is_array());
		EXPECT_TRUE(parsed["types"].empty());
	}
} // namespace

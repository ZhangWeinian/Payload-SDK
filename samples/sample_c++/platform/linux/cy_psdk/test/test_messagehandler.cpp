// cy_psdk/tests/test_messagehandler.cpp
//
// MqttMessageHandler 路由表纯逻辑测试 (无 PSDK/MQTT 依赖)。

#include "manager/mqtt/handler/MessageHandler.h"

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <string>

namespace
{
	using nlohmann::json;
	using plane::manager::MqttMessageHandler;

	// 每个用例独立 topic, 避免单例路由表跨用例污染
	const std::string kRouteTopic { "/unit/handler/route" };
	const std::string kUnknownTypeTopic { "/unit/handler/unknown_type" };
	const std::string kUnknownTopic { "/unit/handler/unknown_topic" };
	const std::string kThrowTopic { "/unit/handler/throw" };
} // namespace

TEST(MqttMessageHandler, DispatchesToRegisteredHandler)
{
	int	 call_count { 0 };
	json last_payload;

	MqttMessageHandler::getInstance().registerHandler(
		kRouteTopic,
		"SBZT",
		[&](const json& payload)
		{
			++call_count;
			last_payload = payload;
		}
	);

	MqttMessageHandler::getInstance().routeMessage(
		kRouteTopic,
		"SBZT",
		json {
			{ "DQJD", 116.39 }
	  }
	);

	EXPECT_EQ(call_count, 1);
	EXPECT_DOUBLE_EQ(last_payload.at("DQJD").get<double>(), 116.39);
}

TEST(MqttMessageHandler, RegistrationOverwritesExistingHandler)
{
	int			first_count { 0 };
	int			second_count { 0 };

	const auto& topic { kRouteTopic };
	MqttMessageHandler::getInstance().registerHandler(
		topic,
		"SBZT",
		[&](const json&)
		{
			++first_count;
		}
	);
	MqttMessageHandler::getInstance().registerHandler(
		topic,
		"SBZT",
		[&](const json&)
		{
			++second_count;
		}
	);

	MqttMessageHandler::getInstance().routeMessage(topic, "SBZT", json::object());

	EXPECT_EQ(first_count, 0);
	EXPECT_EQ(second_count, 1);
}

TEST(MqttMessageHandler, UnregisteredMessageTypeDoesNotCrash)
{
	int call_count { 0 };
	MqttMessageHandler::getInstance().registerHandler(
		kUnknownTypeTopic,
		"SBZT",
		[&](const json&)
		{
			++call_count;
		}
	);

	EXPECT_NO_THROW(MqttMessageHandler::getInstance().routeMessage(kUnknownTypeTopic, "XXXX", json::object()));
	EXPECT_EQ(call_count, 0);
}

TEST(MqttMessageHandler, UnregisteredTopicDoesNotCrash)
{
	EXPECT_NO_THROW(MqttMessageHandler::getInstance().routeMessage(kUnknownTopic, "SBZT", json::object()));
}

TEST(MqttMessageHandler, HandlerExceptionIsSwallowed)
{
	MqttMessageHandler::getInstance().registerHandler(
		kThrowTopic,
		"SBZT",
		[&](const json&)
		{
			throw std::runtime_error("boom");
		}
	);

	EXPECT_NO_THROW(MqttMessageHandler::getInstance().routeMessage(kThrowTopic, "SBZT", json::object()));
}

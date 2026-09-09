// cy_psdk/tests/test_catalog_client.cpp
//
// 自研 catalog 客户端纯逻辑单元测试: targets 展开 + SWMP 探测包编解码。

#include "manager/catalog/client/internal/ProbePacketCodec.h"
#include "manager/catalog/client/internal/TargetExpander.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace
{
	using plane::catalog::internal::expandTargets;
	using plane::catalog::internal::PROBE_ANNOUNCE_COMMAND;
	using plane::catalog::internal::PROBE_COMMAND;
	using plane::catalog::internal::PROBE_RESPONSE_COMMAND;
	using plane::catalog::internal::ProbePacket;

	using plane::catalog::CatalogAnnouncement;
	using plane::catalog::internal::decodeAnnouncement;
	using plane::catalog::internal::decodeProbePacket;
	using plane::catalog::internal::encodeProbePacket;

	_STD vector<_STD string> expand(const _STD vector<_STD string>& specs, int max_count = 1024)
	{
		auto result { expandTargets(specs, max_count) };
		EXPECT_TRUE(result.isOk()) << "expand 应成功: " << result.error().message;
		return result.isOk() ? result.value() : _STD vector<_STD string> {};
	}
} // namespace

// ---- TargetExpander ----

TEST(CatalogTargetExpander, SingleIp)
{
	const auto result { expand({ "192.168.1.131" }) };
	ASSERT_EQ(result.size(), 1u);
	EXPECT_EQ(result[0], "192.168.1.131");
}

TEST(CatalogTargetExpander, WildcardLastOctet)
{
	const auto result { expand({ "192.168.1.*" }) };
	ASSERT_EQ(result.size(), 254u);
	EXPECT_EQ(result.front(), "192.168.1.1");
	EXPECT_EQ(result.back(), "192.168.1.254");
}

TEST(CatalogTargetExpander, Cidr)
{
	const auto result { expand({ "192.168.1.0/30" }) };
	ASSERT_EQ(result.size(), 2u);
	EXPECT_EQ(result[0], "192.168.1.1");
	EXPECT_EQ(result[1], "192.168.1.2");
}

TEST(CatalogTargetExpander, Range)
{
	const auto result { expand({ "192.168.1.5-7" }) };
	ASSERT_EQ(result.size(), 3u);
	EXPECT_EQ(result[0], "192.168.1.5");
	EXPECT_EQ(result[1], "192.168.1.6");
	EXPECT_EQ(result[2], "192.168.1.7");
}

TEST(CatalogTargetExpander, EmptySpecsIsInvalid)
{
	const auto result { expandTargets({}, 1024) };
	EXPECT_FALSE(result.isOk());
}

TEST(CatalogTargetExpander, MalformedSpecIsInvalid)
{
	const auto result { expandTargets({ "192.168.1.1.2" }, 1024) };
	EXPECT_FALSE(result.isOk());
}

// ---- ProbePacketCodec ----

TEST(CatalogProbeCodec, EncodeDecodeRoundTrip)
{
	ProbePacket packet {};
	packet.command		  = PROBE_RESPONSE_COMMAND;
	packet.ip			  = "192.168.1.131";
	packet.node_id		  = "CATF4KHZBF8";
	packet.status		  = 1;
	packet.node_name	  = "SwarmServer";
	packet.request_id	  = 0X12'34'56'78;
	packet.has_request_id = true;
	packet.instance_id	  = "inst-01";
	packet.http_port	  = 8081;

	const auto encoded { encodeProbePacket(packet) };
	ASSERT_TRUE(encoded.isOk());

	const auto decoded { decodeProbePacket(encoded.value()) };
	ASSERT_TRUE(decoded.isOk());

	EXPECT_EQ(decoded.value().command, packet.command);
	EXPECT_EQ(decoded.value().ip, packet.ip);
	EXPECT_EQ(decoded.value().node_id, packet.node_id);
	EXPECT_EQ(decoded.value().status, packet.status);
	EXPECT_EQ(decoded.value().node_name, packet.node_name);
	EXPECT_EQ(decoded.value().request_id, packet.request_id);
	EXPECT_TRUE(decoded.value().has_request_id);
	EXPECT_EQ(decoded.value().instance_id, packet.instance_id);
	EXPECT_EQ(decoded.value().http_port, packet.http_port);
}

TEST(CatalogProbeCodec, DecodeAnnouncement)
{
	ProbePacket packet {};
	packet.command	   = PROBE_ANNOUNCE_COMMAND;
	packet.node_id	   = "SwarmServer";
	packet.node_name   = "主目录";
	packet.instance_id = "catalog-1";
	packet.http_port   = 8081;

	const auto encoded { encodeProbePacket(packet) };
	ASSERT_TRUE(encoded.isOk());

	const auto decoded { decodeAnnouncement(encoded.value()) };
	ASSERT_TRUE(decoded.isOk());

	EXPECT_EQ(decoded.value().node_id, "SwarmServer");
	EXPECT_EQ(decoded.value().node_name, "主目录");
	EXPECT_EQ(decoded.value().instance_id, "catalog-1");
	EXPECT_EQ(decoded.value().http_port, 8081);
}

TEST(CatalogProbeCodec, WrongMagicIsRejected)
{
	_STD vector<std::uint8_t> garbage { 0X00, 0X01, 0X02, 0X03, 0X04, 0X05, 0X06, 0X07, 0X08 };
	const auto				  decoded { decodeProbePacket(garbage) };
	EXPECT_FALSE(decoded.isOk());
}

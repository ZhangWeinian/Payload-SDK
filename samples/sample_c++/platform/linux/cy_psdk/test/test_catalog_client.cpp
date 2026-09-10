// cy_psdk/tests/test_catalog_client.cpp
//
// 自研 catalog 客户端纯逻辑单元测试: targets 展开 + SWMP 探测包编解码。

#include "manager/catalog/client/internal/codec/JsonCodec.h"
#include "manager/catalog/client/internal/codec/ProbePacketCodec.h"
#include "manager/catalog/client/internal/discovery/CatalogIpCache.h"
#include "manager/catalog/client/internal/util/TargetExpander.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
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
		EXPECT_TRUE(result.has_value()) << "expand 应成功: " << result.error().message;
		return result.has_value() ? result.value() : _STD vector<_STD string> {};
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
	EXPECT_FALSE(result.has_value());
}

TEST(CatalogTargetExpander, MalformedSpecIsInvalid)
{
	const auto result { expandTargets({ "192.168.1.1.2" }, 1024) };
	EXPECT_FALSE(result.has_value());
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
	ASSERT_TRUE(encoded.has_value());

	const auto decoded { decodeProbePacket(encoded.value()) };
	ASSERT_TRUE(decoded.has_value());

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
	ASSERT_TRUE(encoded.has_value());

	const auto decoded { decodeAnnouncement(encoded.value()) };
	ASSERT_TRUE(decoded.has_value());

	EXPECT_EQ(decoded.value().node_id, "SwarmServer");
	EXPECT_EQ(decoded.value().node_name, "主目录");
	EXPECT_EQ(decoded.value().instance_id, "catalog-1");
	EXPECT_EQ(decoded.value().http_port, 8081);
}

TEST(CatalogProbeCodec, WrongMagicIsRejected)
{
	_STD vector<_STD uint8_t> garbage { 0X00, 0X01, 0X02, 0X03, 0X04, 0X05, 0X06, 0X07, 0X08 };
	const auto				  decoded { decodeProbePacket(garbage) };
	EXPECT_FALSE(decoded.has_value());
}

// ---- 严格解析与 trim 语义 (对齐 java) ----

TEST(CatalogTargetExpander, TrimsSpecsAndRejectsLooseOctet)
{
	const auto trimmed { expand({ " 192.168.1.131 " }) };
	ASSERT_EQ(trimmed.size(), 1u);
	EXPECT_EQ(trimmed[0], "192.168.1.131");

	// 严格解析: 非纯数字/超范围/非法前缀均为非法 (对齐 java Integer.parseInt)
	EXPECT_FALSE(expandTargets({ "192.168.1.1x" }, 1024).has_value());
	EXPECT_FALSE(expandTargets({ "192.168.1.+1" }, 1024).has_value());
	EXPECT_FALSE(expandTargets({ "192.168.1.256" }, 1024).has_value());
	EXPECT_FALSE(expandTargets({ "192.168.1.1/33" }, 1024).has_value());
	EXPECT_FALSE(expandTargets({ "192.168.1.1/a" }, 1024).has_value());
}

TEST(CatalogJsonCodec, NormalizeVersionTrimsOnly)
{
	const auto trimmed { plane::catalog::internal::JsonCodec::normalizeVersion("  1.0.0  ") };
	ASSERT_TRUE(trimmed.has_value());
	EXPECT_EQ(trimmed.value(), "1.0.0");

	// 内部空白保留 (对齐 java trim 语义, 不再删除内部字符)
	const auto inner { plane::catalog::internal::JsonCodec::normalizeVersion("1.0.0 beta") };
	ASSERT_TRUE(inner.has_value());
	EXPECT_EQ(inner.value(), "1.0.0 beta");

	EXPECT_FALSE(plane::catalog::internal::JsonCodec::normalizeVersion("   ").has_value());
}

TEST(CatalogIpCacheTest, SaveAndPrioritizeRoundTrip)
{
	using plane::catalog::internal::CatalogIpCache;

	const auto		dir { _STD filesystem::temp_directory_path() / "cy_psdk_catalog_ipcache_test" };
	const auto		file { dir / "last_catalog_ip" };
	_STD error_code ec {};
	_STD			filesystem::remove_all(dir, ec);

	CatalogIpCache	cache { file.string() };
	cache.save("192.168.1.77");

	ASSERT_TRUE(_STD filesystem::exists(file));
	{
		_STD ifstream in { file, _STD ios::binary };
		_STD string	  line {};
		_STD		  getline(in, line);
		EXPECT_EQ(line, "192.168.1.77");
	}

	const auto reordered { cache.prioritize({ "10.0.0.1", "192.168.1.77", "10.0.0.2" }) };
	ASSERT_EQ(reordered.size(), 3u);
	EXPECT_EQ(reordered[0], "192.168.1.77");

	// 非法 IP 不写入
	cache.save("192.168.1.300");
	{
		_STD ifstream in { file, _STD ios::binary };
		_STD string	  line {};
		_STD		  getline(in, line);
		EXPECT_EQ(line, "192.168.1.77");
	}
	_STD filesystem::remove_all(dir, ec);
}

// cy_psdk/tests/test_catalog_client.cpp
//
// 自研 catalog 客户端纯逻辑单元测试: targets 展开 + SWMP 探测包编解码。

#include "manager/catalog/client/internal/codec/JsonCodec.h"
#include "manager/catalog/client/internal/codec/ProbePacketCodec.h"
#include "manager/catalog/client/internal/discovery/CatalogIpCache.h"
#include "manager/catalog/client/internal/util/Base64.h"
#include "manager/catalog/client/internal/util/TargetExpander.h"
#include "manager/catalog/client/internal/util/TextUtil.h"

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
    using plane::catalog::CatalogError;
    using plane::catalog::Result;
    using plane::catalog::internal::decodeAnnouncement;
    using plane::catalog::internal::decodeBase64;
    using plane::catalog::internal::decodeProbePacket;
    using plane::catalog::internal::encodeProbePacket;
    using plane::catalog::internal::isValidUtf8;

    ::std::vector<::std::string> expand(const ::std::vector<::std::string>& specs, int max_count = 1024)
    {
        auto result { expandTargets(specs, max_count) };
        EXPECT_TRUE(result.has_value()) << "expand 应成功: " << result.error().message;
        return result.has_value() ? result.value() : ::std::vector<::std::string> {};
    }
} // namespace

// TargetExpander

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

// ProbePacketCodec

TEST(CatalogProbeCodec, EncodeDecodeRoundTrip)
{
    ProbePacket packet {};
    packet.command           = PROBE_RESPONSE_COMMAND;
    packet.ip                = "192.168.1.131";
    packet.node_id           = "CATF4KHZBF8";
    packet.status            = 1;
    packet.node_name         = "SwarmServer";
    packet.request_id        = 0X12'34'56'78;
    packet.has_request_id    = true;
    packet.instance_id       = "inst-01";
    packet.http_port         = 8081;

    packet.multicast_address = "239.255.1.9";
    packet.multicast_port    = 31'094;

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
    EXPECT_EQ(decoded.value().multicast_address, packet.multicast_address);
    EXPECT_EQ(decoded.value().multicast_port, packet.multicast_port);
}

// 回归: 锁定字段顺序 (对齐服务端 / java)
// 服务端把 nodeName 放在包尾、组播字段追加在其后; 早期实现把 nodeName 排在 requestId
// 之前, 会让 requestId 错位并丢弃全部响应 (发现不到节点)。
// 字节序列手工构造, 防止两侧同时改错也测不出来。
TEST(CatalogProbeCodec, DecodeAcceptsUpstreamLayoutWithTrailingNodeNameAndMulticast)
{
    const ::std::vector<::std::uint8_t> upstream {
        0X53, 0X57, 0X4d, 0X50,                                         // MAGIC "SWMP"
        0X01, 0X82,                                                     // VERSION / CMD=公告
        0X09, '1',  '0',  '.',  '1', '0', '.', '1', '.', '9',           // ip = 10.10.1.9
        0X05, 'N',  'O',  'D',  'E', 'A',                               // nodeId = NODEA
        0X01,                                                           // status = 1
        0X00, 0X00, 0X00, 0X2a,                                         // requestId = 42
        0X03, 'I',  'N',  'S',                                          // instanceId = INS
        0X78, 0Xba,                                                     // httpPort = 30906
        0X06, 'C',  'A',  'T',  '-', '0', '1',                          // nodeName (包尾)
        0X0b, '2',  '3',  '9',  '.', '2', '5', '5', '.', '1', '.', '9', // multicastAddress
        0X79, 0X76                                                      // multicastPort = 31094
    };

    const auto decoded { decodeProbePacket(upstream) };
    ASSERT_TRUE(decoded.has_value());

    EXPECT_EQ(decoded.value().command, PROBE_ANNOUNCE_COMMAND);
    EXPECT_EQ(decoded.value().ip, "10.10.1.9");
    EXPECT_EQ(decoded.value().node_id, "NODEA");
    EXPECT_EQ(decoded.value().status, 1);
    EXPECT_EQ(decoded.value().request_id, 42);
    EXPECT_TRUE(decoded.value().has_request_id);
    EXPECT_EQ(decoded.value().instance_id, "INS");
    EXPECT_EQ(decoded.value().http_port, 30'906);
    EXPECT_EQ(decoded.value().node_name, "CAT-01");
    EXPECT_EQ(decoded.value().multicast_address, "239.255.1.9");
    EXPECT_EQ(decoded.value().multicast_port, 31'094);

    // 公告包解析同样透出节点名/实例/端口
    const auto announcement { decodeAnnouncement(upstream) };
    ASSERT_TRUE(announcement.has_value());
    EXPECT_EQ(announcement.value().node_id, "NODEA");
    EXPECT_EQ(announcement.value().node_name, "CAT-01");
    EXPECT_EQ(announcement.value().instance_id, "INS");
    EXPECT_EQ(announcement.value().http_port, 30'906);
}

// 兼容旧端点: status 之后直接结束 (未写 requestId/instanceId/port/nodeName/组播)
TEST(CatalogProbeCodec, DecodeToleratesLegacyPacketWithoutTrailingFields)
{
    const ::std::vector<::std::uint8_t> legacy {
        0X53, 0X57, 0X4d, 0X50, 0X01, 0X81, // MAGIC / VERSION / CMD=响应
        0X02, 'i',  'p',                    // ip = ip
        0X03, 'n',  'o',  'd',              // nodeId = nod
        0X01                                // status = 1
    };

    const auto decoded { decodeProbePacket(legacy) };
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded.value().command, PROBE_RESPONSE_COMMAND);
    EXPECT_EQ(decoded.value().ip, "ip");
    EXPECT_EQ(decoded.value().node_id, "nod");
    EXPECT_EQ(decoded.value().status, 1);
    EXPECT_FALSE(decoded.value().has_request_id);
    EXPECT_EQ(decoded.value().instance_id, "");
    EXPECT_EQ(decoded.value().http_port, 0);
    EXPECT_EQ(decoded.value().node_name, "");
    EXPECT_EQ(decoded.value().multicast_address, "");
    EXPECT_EQ(decoded.value().multicast_port, 0);
}

TEST(CatalogProbeCodec, DecodeAnnouncement)
{
    ProbePacket packet {};
    packet.command     = PROBE_ANNOUNCE_COMMAND;
    packet.node_id     = "SwarmServer";
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
    ::std::vector<::std::uint8_t> garbage { 0X00, 0X01, 0X02, 0X03, 0X04, 0X05, 0X06, 0X07, 0X08 };
    const auto                    decoded { decodeProbePacket(garbage) };
    EXPECT_FALSE(decoded.has_value());
}

// Base64 解码 (数据池 payloadBase64)

TEST(CatalogBase64, DecodesWithAndWithoutPadding)
{
    const auto text = [](const Result<::std::vector<::std::uint8_t>>& value)
    {
        EXPECT_TRUE(value.has_value()) << (value.has_value() ? "" : value.error().message);
        return value.has_value() ? ::std::string { value.value().begin(), value.value().end() } : ::std::string {};
    };

    EXPECT_EQ(text(decodeBase64("TWFu")), "Man"); // 无填充
    EXPECT_EQ(text(decodeBase64("TWE=")), "Ma");  // 一个填充
    EXPECT_EQ(text(decodeBase64("TQ==")), "M");   // 两个填充
    EXPECT_EQ(text(decodeBase64("")), "");
}

TEST(CatalogBase64, RejectsMalformedInput)
{
    const auto is_protocol_error = [](const Result<::std::vector<::std::uint8_t>>& value)
    {
        return !value.has_value() && value.error().code == CatalogError::PROTOCOL_ERROR;
    };

    EXPECT_TRUE(is_protocol_error(decodeBase64("TWE")));      // 长度不是 4 的倍数
    EXPECT_TRUE(is_protocol_error(decodeBase64("TW*=")));     // 非法字符
    EXPECT_TRUE(is_protocol_error(decodeBase64("T=Fu")));     // 填充出现在中间
    EXPECT_TRUE(is_protocol_error(decodeBase64("T===")));     // 填充超过两个
    EXPECT_TRUE(is_protocol_error(decodeBase64("AAAA===="))); // 填充出现在非末组
}

// 严格 UTF-8 校验 (数据池正文)

TEST(CatalogUtf8, ValidatesStrictly)
{
    const auto bytes = [](::std::initializer_list<int> values)
    {
        ::std::vector<::std::uint8_t> out {};
        out.reserve(values.size());
        for (const int value : values)
        {
            out.push_back(static_cast<::std::uint8_t>(value));
        }
        return out;
    };

    EXPECT_TRUE(isValidUtf8(bytes({})));
    EXPECT_TRUE(isValidUtf8(bytes({ 'h', 'i' })));
    EXPECT_TRUE(isValidUtf8(bytes({ 0Xe4, 0Xb8, 0Xad })));        // "中"
    EXPECT_TRUE(isValidUtf8(bytes({ 0Xf0, 0X9f, 0X98, 0X80 })));  // 四字节表情

    EXPECT_FALSE(isValidUtf8(bytes({ 0Xe4, 0Xb8 })));             // 被截断
    EXPECT_FALSE(isValidUtf8(bytes({ 0X80 })));                   // 孤立续字节
    EXPECT_FALSE(isValidUtf8(bytes({ 0Xc0, 0X80 })));             // 过长编码
    EXPECT_FALSE(isValidUtf8(bytes({ 0Xe0, 0X80, 0X80 })));       // 过长编码 (三字节)
    EXPECT_FALSE(isValidUtf8(bytes({ 0Xed, 0Xa0, 0X80 })));       // 代理区码点
    EXPECT_FALSE(isValidUtf8(bytes({ 0Xf5, 0X80, 0X80, 0X80 }))); // 超出 U+10FFFF 起始字节
}

// 严格解析与 trim 语义 (对齐 java)

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

    const auto        dir { ::std::filesystem::temp_directory_path() / "cy_psdk_catalog_ipcache_test" };
    const auto        file { dir / "last_catalog_ip" };
    ::std::error_code ec {};
    ::std::filesystem::remove_all(dir, ec);

    CatalogIpCache cache { file.string() };
    cache.save("192.168.1.77");

    ASSERT_TRUE(::std::filesystem::exists(file));
    {
        ::std::ifstream in { file, ::std::ios::binary };
        ::std::string   line {};
        ::std::getline(in, line);
        EXPECT_EQ(line, "192.168.1.77");
    }

    const auto reordered { cache.prioritize({ "10.0.0.1", "192.168.1.77", "10.0.0.2" }) };
    ASSERT_EQ(reordered.size(), 3u);
    EXPECT_EQ(reordered[0], "192.168.1.77");

    // 非法 IP 不写入
    cache.save("192.168.1.300");
    {
        ::std::ifstream in { file, ::std::ios::binary };
        ::std::string   line {};
        ::std::getline(in, line);
        EXPECT_EQ(line, "192.168.1.77");
    }
    ::std::filesystem::remove_all(dir, ec);
}

// cy_psdk/tests/test_udp_discovery_client.cpp
//
// UdpDiscoveryClient 单元测试: 参数校验 + 环回 UDP 真实 socket 发现流程。
// 覆盖: 非法参数 / 取消 / 无响应超时 / 单实例发现 / 字段过滤 / 多实例与去重。

#include "manager/catalog/client/internal/discovery/UdpDiscoveryClient.h"

#include "manager/catalog/client/internal/codec/ProbePacketCodec.h"

#include <gtest/gtest.h>

#include <asio.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace
{
    using plane::catalog::DiscoveryConfig;
    using plane::catalog::internal::decodeProbePacket;
    using plane::catalog::internal::DiscoveryReport;
    using plane::catalog::internal::DiscoveryStatus;
    using plane::catalog::internal::encodeProbePacket;
    using plane::catalog::internal::PROBE_RESPONSE_COMMAND;
    using plane::catalog::internal::ProbePacket;
    using plane::catalog::internal::UdpDiscoveryClient;

    // 环回单目标发现配置
    DiscoveryConfig makeConfig(int port, ::std::chrono::milliseconds window = ::std::chrono::milliseconds { 300 })
    {
        DiscoveryConfig config {};
        config.node_id         = "node-under-test";
        config.port            = port;
        config.targets         = { "127.0.0.1" };
        config.response_window = window;
        return config;
    }

    // 环回 UDP 应答器: 绑定随机端口; 收到探测请求后经 mutator 加工并回发响应 (可多份)
    struct LoopbackResponder
    {
        ::asio::io_context      io {};
        ::asio::ip::udp::socket socket { io };
        int                     port { 0 };

        LoopbackResponder(void)
        {
            this->socket.open(::asio::ip::udp::v4());
            this->socket.bind(::asio::ip::udp::endpoint { ::asio::ip::udp::v4(), 0 });
            this->port = static_cast<int>(this->socket.local_endpoint().port());
        }

        template<typename Mutator>
        void serveOnce(Mutator mutate, int response_count = 1, ::std::chrono::milliseconds timeout = ::std::chrono::milliseconds { 5000 })
        {
            this->socket.non_blocking(true);
            ::std::vector<::std::uint8_t> buffer(1024);
            ::asio::ip::udp::endpoint     from {};
            const auto                    deadline { ::std::chrono::steady_clock::now() + timeout };
            while (::std::chrono::steady_clock::now() < deadline)
            {
                ::asio::error_code  ec {};
                const ::std::size_t length { this->socket.receive_from(::asio::buffer(buffer), from, 0, ec) };
                if (ec == ::asio::error::would_block || ec == ::asio::error::try_again)
                {
                    ::std::this_thread::sleep_for(::std::chrono::milliseconds { 2 });
                    continue;
                }
                if (ec)
                {
                    return;
                }
                const auto request { decodeProbePacket({ buffer.begin(), buffer.begin() + static_cast<::std::ptrdiff_t>(length) }) };
                if (!request.has_value())
                {
                    return;
                }
                for (int index { 0 }; index < response_count; ++index)
                {
                    ProbePacket response {};
                    response.command        = PROBE_RESPONSE_COMMAND;
                    response.node_id        = request.value().node_id;
                    response.request_id     = request.value().request_id;
                    response.has_request_id = true;
                    response.status         = 1;
                    response.http_port      = 8081;
                    response.instance_id    = "inst-1";
                    response.node_name      = "node-x";
                    mutate(response, index);
                    const auto encoded { encodeProbePacket(response) };
                    if (!encoded.has_value())
                    {
                        return;
                    }
                    this->socket.send_to(::asio::buffer(encoded.value()), from, 0, ec);
                }
                return;
            }
        }
    };
} // namespace

// 参数校验 (不涉及 socket 收发)

TEST(UdpDiscoveryClientTest, RejectsInvalidArguments)
{
    UdpDiscoveryClient        client {};
    const ::std::atomic<bool> cancelled { false };

    {
        DiscoveryConfig config { makeConfig(30'906) };
        config.node_id.clear();
        const DiscoveryReport report { client.discover(config, cancelled) };
        EXPECT_EQ(report.status, DiscoveryStatus::INVALID_ARGUMENT);
        EXPECT_EQ(report.error, "nodeId 为空");
    }
    {
        const DiscoveryReport report { client.discover(makeConfig(0), cancelled) };
        EXPECT_EQ(report.status, DiscoveryStatus::INVALID_ARGUMENT);
    }
    {
        const DiscoveryReport report { client.discover(makeConfig(65'536), cancelled) };
        EXPECT_EQ(report.status, DiscoveryStatus::INVALID_ARGUMENT);
    }
    {
        const DiscoveryReport report { client.discover(makeConfig(30'906, ::std::chrono::milliseconds { 0 }), cancelled) };
        EXPECT_EQ(report.status, DiscoveryStatus::INVALID_ARGUMENT);
    }
    {
        DiscoveryConfig config { makeConfig(30'906) };
        config.targets = { "not-an-ip" };
        const DiscoveryReport report { client.discover(config, cancelled) };
        EXPECT_EQ(report.status, DiscoveryStatus::INVALID_ARGUMENT);
    }
}

// 编码失败: 字段超长 (单字节长度前缀, 上限 255)

TEST(UdpDiscoveryClientTest, RejectsOverlongNodeIdAtEncoding)
{
    UdpDiscoveryClient        client {};
    const ::std::atomic<bool> cancelled { false };
    DiscoveryConfig           config { makeConfig(30'906) };
    config.node_id = ::std::string(300, 'n');
    const DiscoveryReport report { client.discover(config, cancelled) };
    EXPECT_EQ(report.status, DiscoveryStatus::INVALID_ARGUMENT);
}

// 取消: 首轮发送检查即返回

TEST(UdpDiscoveryClientTest, CancelledBeforeSending)
{
    UdpDiscoveryClient        client {};
    const ::std::atomic<bool> cancelled { true };
    const DiscoveryReport     report { client.discover(makeConfig(30'906), cancelled) };
    EXPECT_EQ(report.status, DiscoveryStatus::NOT_FOUND);
    EXPECT_EQ(report.error, "探测已取消");
}

// 无响应: 等待整个响应窗口后 NOT_FOUND

TEST(UdpDiscoveryClientTest, NotFoundWithoutResponder)
{
    const LoopbackResponder   responder {}; // 端口已绑定, 但无人回包
    UdpDiscoveryClient        client {};
    const ::std::atomic<bool> cancelled { false };
    const DiscoveryReport     report { client.discover(makeConfig(responder.port, ::std::chrono::milliseconds { 150 }), cancelled) };
    EXPECT_EQ(report.status, DiscoveryStatus::NOT_FOUND);
    EXPECT_TRUE(report.endpoints.empty());
}

// 绑定地址失效: 回退通配绑定后仍可发现

TEST(UdpDiscoveryClientTest, FallsBackToWildcardBindWhenAddressInvalid)
{
    LoopbackResponder responder {};
    ::std::thread     responder_thread { [&responder]
                                         {
                                         responder.serveOnce([](ProbePacket&, int) {});
                                     } };

    // 192.0.2.0/24 为 TEST-NET (RFC 5737), 本机必不存在该地址 -> 绑定失败回退通配
    UdpDiscoveryClient        client { ::std::nullopt, "192.0.2.1" };
    const ::std::atomic<bool> cancelled { false };
    const DiscoveryReport     report { client.discover(makeConfig(responder.port), cancelled) };
    responder_thread.join();

    EXPECT_EQ(report.status, DiscoveryStatus::OK);
}

// 环回发现: 完整收发/编解码/上报链路

TEST(UdpDiscoveryClientTest, DiscoversResponderOnLoopback)
{
    LoopbackResponder responder {};
    ::std::thread     responder_thread { [&responder]
                                         {
                                         responder.serveOnce([](ProbePacket&, int) {});
                                     } };

    UdpDiscoveryClient        client {};
    const ::std::atomic<bool> cancelled { false };
    const DiscoveryReport     report { client.discover(makeConfig(responder.port), cancelled) };
    responder_thread.join();

    ASSERT_EQ(report.status, DiscoveryStatus::OK);
    ASSERT_EQ(report.endpoints.size(), 1u);
    EXPECT_EQ(report.endpoints[0].ip, "127.0.0.1"); // 响应未携带 ip: 回退为源地址
    EXPECT_EQ(report.endpoints[0].instance_id, "inst-1");
    EXPECT_EQ(report.endpoints[0].http_port, 8081);
    EXPECT_EQ(report.endpoints[0].node_name, "node-x");
    EXPECT_FALSE(report.multiple_instances);
}

// 过滤: nodeId / httpPort / status 不符合则不采纳

TEST(UdpDiscoveryClientTest, IgnoresForeignNodeId)
{
    LoopbackResponder responder {};
    ::std::thread     responder_thread { [&responder]
                                         {
                                         responder.serveOnce(
                                             [](ProbePacket& response, int)
                                             {
                                                 response.node_id = "other-node";
                                             }
                                         );
                                     } };

    UdpDiscoveryClient        client {};
    const ::std::atomic<bool> cancelled { false };
    const DiscoveryReport     report { client.discover(makeConfig(responder.port), cancelled) };
    responder_thread.join();

    EXPECT_EQ(report.status, DiscoveryStatus::NOT_FOUND);
}

TEST(UdpDiscoveryClientTest, IgnoresZeroHttpPort)
{
    LoopbackResponder responder {};
    ::std::thread     responder_thread { [&responder]
                                         {
                                         responder.serveOnce(
                                             [](ProbePacket& response, int)
                                             {
                                                 response.http_port = 0;
                                             }
                                         );
                                     } };

    UdpDiscoveryClient        client {};
    const ::std::atomic<bool> cancelled { false };
    const DiscoveryReport     report { client.discover(makeConfig(responder.port), cancelled) };
    responder_thread.join();

    EXPECT_EQ(report.status, DiscoveryStatus::NOT_FOUND);
}

TEST(UdpDiscoveryClientTest, StatusZeroIsNotAdopted)
{
    LoopbackResponder responder {};
    ::std::thread     responder_thread { [&responder]
                                         {
                                         responder.serveOnce(
                                             [](ProbePacket& response, int)
                                             {
                                                 response.status = 0;
                                             }
                                         );
                                     } };

    UdpDiscoveryClient        client {};
    const ::std::atomic<bool> cancelled { false };
    const DiscoveryReport     report { client.discover(makeConfig(responder.port), cancelled) };
    responder_thread.join();

    EXPECT_EQ(report.status, DiscoveryStatus::NOT_FOUND);
    EXPECT_TRUE(report.endpoints.empty());
}

// 多实例与去重

TEST(UdpDiscoveryClientTest, MultipleInstances)
{
    LoopbackResponder responder {};
    ::std::thread     responder_thread { [&responder]
                                         {
                                         responder.serveOnce(
                                             [](ProbePacket& response, int index)
                                             {
                                                 response.instance_id = index == 0 ? "inst-1" : "inst-2";
                                             },
                                             2
                                         );
                                     } };

    UdpDiscoveryClient        client {};
    const ::std::atomic<bool> cancelled { false };
    const DiscoveryReport     report { client.discover(makeConfig(responder.port), cancelled) };
    responder_thread.join();

    ASSERT_EQ(report.status, DiscoveryStatus::OK);
    EXPECT_EQ(report.endpoints.size(), 2u);
    EXPECT_TRUE(report.multiple_instances);
}

TEST(UdpDiscoveryClientTest, DuplicateInstanceKeepsFirst)
{
    LoopbackResponder responder {};
    ::std::thread     responder_thread { [&responder]
                                         {
                                         responder.serveOnce([](ProbePacket&, int) {}, 2);
                                     } }; // 同一实例回发两次

    UdpDiscoveryClient        client {};
    const ::std::atomic<bool> cancelled { false };
    const DiscoveryReport     report { client.discover(makeConfig(responder.port), cancelled) };
    responder_thread.join();

    ASSERT_EQ(report.status, DiscoveryStatus::OK);
    EXPECT_EQ(report.endpoints.size(), 1u); // 去重保留首包
    EXPECT_FALSE(report.multiple_instances);
}

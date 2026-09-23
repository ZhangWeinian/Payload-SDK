// cy_psdk/test/test_catalog_runtime.cpp
//
// catalog 客户端集成测试 (注入式, 不依赖真实网络):
//   - CatalogRuntime 使用注入的 HttpTransport / DiscoveryClient (回归: 注入必须生效)
//   - 注册主链: start -> registerServiceInstance -> READY; 409 幂等注册视为成功
//   - ServiceGateway: healthy/enabled 过滤、健康状态大小写不敏感
//   - UdpAnnouncementListener: 回环接收 SWMP command=0x82 公告

#include <gtest/gtest.h>

#include <asio.hpp>
#include <httplib.h>

#include <condition_variable>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "define.h"
#include "manager/catalog/client/CatalogRuntime.h"
#include "manager/catalog/client/CatalogRuntimeOptions.h"
#include "manager/catalog/client/CatalogTypes.h"
#include "manager/catalog/client/DiscoveryConfig.h"
#include "manager/catalog/client/internal/codec/ProbePacketCodec.h"
#include "manager/catalog/client/internal/discovery/DiscoveryClient.h"
#include "manager/catalog/client/internal/discovery/UdpAnnouncementListener.h"
#include "manager/catalog/client/internal/discovery/UdpMulticastAnnouncementListener.h"
#include "manager/catalog/client/internal/service/ServiceGateway.h"
#include "manager/catalog/client/internal/transport/CppHttpTransport.h"
#include "manager/catalog/client/internal/transport/HttpTransport.h"

namespace
{
    using plane::catalog::CatalogEndpoint;
    using plane::catalog::CatalogError;
    using plane::catalog::CatalogRuntime;
    using plane::catalog::CatalogRuntimeOptions;
    using plane::catalog::CatalogState;
    using plane::catalog::DiscoveryConfig;
    using plane::catalog::ServiceQuery;
    using plane::catalog::internal::DiscoveryClient;
    using plane::catalog::internal::DiscoveryReport;
    using plane::catalog::internal::DiscoveryStatus;
    using plane::catalog::internal::HttpResponseData;
    using plane::catalog::internal::HttpTransport;
    using plane::catalog::internal::ServiceGateway;
    using plane::catalog::internal::UdpAnnouncementListener;
    using plane::catalog::internal::UdpMulticastAnnouncementListener;

    using Clock = ::std::chrono::steady_clock;

    // 注入测试替身

    class FakeHttpTransport final: public HttpTransport
    {
    public:
        // handler: (method, url, body) -> response; 为空时统一返回 200 {"id":"test-instance"}
        ::std::function<HttpResponseData(const ::std::string&, const ::std::string&, const ::std::string&)> handler {};

        HttpResponseData get(const ::std::string& url) override
        {
            return this->dispatch("GET", url, "");
        }

        HttpResponseData post(const ::std::string& url, const ::std::string& body) override
        {
            return this->dispatch("POST", url, body);
        }

        HttpResponseData put(const ::std::string& url, const ::std::string& body) override
        {
            return this->dispatch("PUT", url, body);
        }

        HttpResponseData del(const ::std::string& url) override
        {
            return this->dispatch("DELETE", url, "");
        }

        void                        setTimeout(::std::chrono::milliseconds) override {}

        [[nodiscard]] ::std::size_t calls(void) const
        {
            ::std::lock_guard<::std::mutex> lock { this->mutex };
            return this->count;
        }

        [[nodiscard]] bool sawCall(const ::std::string& method, const ::std::string& url_fragment) const
        {
            ::std::lock_guard<::std::mutex> lock { this->mutex };
            for (const auto& call : this->log)
            {
                if (call.rfind(method + " ", 0) == 0 && call.find(url_fragment) != ::std::string::npos)
                {
                    return true;
                }
            }
            return false;
        }

    private:
        HttpResponseData dispatch(const ::std::string& method, const ::std::string& url, const ::std::string& body)
        {
            {
                ::std::lock_guard<::std::mutex> lock { this->mutex };
                ++this->count;
                this->log.push_back(method + " " + url);
            }
            if (this->handler)
            {
                return this->handler(method, url, body);
            }
            return okJson(R"({"id":"test-instance"})");
        }

        static HttpResponseData okJson(const ::std::string& body)
        {
            HttpResponseData response {};
            response.transport_ok = true;
            response.status       = 200;
            response.body         = body;
            return response;
        }

        mutable ::std::mutex         mutex {};
        ::std::size_t                count { 0 };
        ::std::vector<::std::string> log {};
    };

    class FakeDiscoveryClient final: public DiscoveryClient
    {
    public:
        DiscoveryReport discover(const DiscoveryConfig&, const ::std::atomic<bool>&) override
        {
            DiscoveryReport report {};
            report.status = DiscoveryStatus::OK;
            report.endpoints.push_back(this->endpoint);
            return report;
        }

        void            saveSuccessfulIp(const ::std::string&) override {}

        CatalogEndpoint endpoint {};
    };

    [[nodiscard]] bool waitForState(const CatalogRuntime& runtime, CatalogState expected, ::std::chrono::milliseconds timeout)
    {
        const auto deadline { Clock::now() + timeout };
        while (Clock::now() < deadline)
        {
            if (runtime.state() == expected)
            {
                return true;
            }
            ::std::this_thread::sleep_for(::std::chrono::milliseconds { 10 });
        }
        return runtime.state() == expected;
    }

    struct RuntimeDeps
    {
        ::std::unique_ptr<FakeHttpTransport>   transport {};
        FakeHttpTransport*                     transport_raw { nullptr };
        ::std::unique_ptr<FakeDiscoveryClient> discovery {};
    };

    [[nodiscard]] RuntimeDeps makeDeps(void)
    {
        RuntimeDeps deps {};
        deps.transport                       = ::std::make_unique<FakeHttpTransport>();
        deps.transport_raw                   = deps.transport.get();
        deps.discovery                       = ::std::make_unique<FakeDiscoveryClient>();
        deps.discovery->endpoint.instance_id = "catalog-1";
        deps.discovery->endpoint.ip          = "127.0.0.1";
        deps.discovery->endpoint.http_port   = 18'081;
        deps.discovery->endpoint.node_name   = "test-catalog";
        return deps;
    }
} // namespace

// CatalogRuntime 注入与注册主链

TEST(CatalogRuntimeInjection, UsesInjectedTransportAndReachesReady)
{
    auto                  deps { makeDeps() };

    CatalogRuntimeOptions options {};
    options.registration.service_id   = "swarm.test.device";
    options.registration.service_name = "测试设备";
    options.registration.version      = "1.0.0";

    DiscoveryConfig discovery_config {};
    discovery_config.node_id = "TESTNODE";
    discovery_config.port    = 30'906;
    discovery_config.targets = { "127.0.0.1" };

    CatalogRuntime runtime { options, discovery_config, ::std::move(deps.transport), ::std::move(deps.discovery) };

    ASSERT_TRUE(runtime.start().has_value());
    ASSERT_TRUE(runtime.registerServiceInstance().has_value());
    ASSERT_TRUE(waitForState(runtime, CatalogState::READY, ::std::chrono::seconds(3)));

    // 注入的 transport 必须真实收到注册请求 (回归: 注入不被丢弃)
    EXPECT_TRUE(deps.transport_raw->sawCall("POST", "/api/registry/services"));
    EXPECT_GT(deps.transport_raw->calls(), 0u);
    EXPECT_EQ(runtime.instanceId(), "test-instance");

    ASSERT_TRUE(runtime.stop(::std::chrono::seconds(1)).has_value());
}

TEST(CatalogRuntimeInjection, IdempotentRegisterConflictIsAccepted)
{
    auto deps { makeDeps() };
    deps.transport_raw->handler = [](const ::std::string& method, const ::std::string& url, const ::std::string&) -> HttpResponseData
    {
        HttpResponseData response {};
        response.transport_ok = true;
        if (method == "POST" && url.find("/instances") != ::std::string::npos && url.find("/heartbeat") == ::std::string::npos)
        {
            // 服务端幂等: 409 且携带 id -> 视为注册成功
            response.status = 409;
            response.body   = R"({"code":"ALREADY_REGISTERED","id":"existing-instance"})";
            return response;
        }
        response.status = 200;
        response.body   = R"({"id":"existing-instance"})";
        return response;
    };

    CatalogRuntimeOptions options {};
    options.registration.service_id   = "swarm.test.device";
    options.registration.service_name = "测试设备";
    options.registration.version      = "1.0.0";

    DiscoveryConfig discovery_config {};
    discovery_config.node_id = "TESTNODE";
    discovery_config.port    = 30'906;
    discovery_config.targets = { "127.0.0.1" };

    CatalogRuntime runtime { options, discovery_config, ::std::move(deps.transport), ::std::move(deps.discovery) };

    ASSERT_TRUE(runtime.start().has_value());
    ASSERT_TRUE(runtime.registerServiceInstance().has_value());
    ASSERT_TRUE(waitForState(runtime, CatalogState::READY, ::std::chrono::seconds(3)));
    EXPECT_EQ(runtime.instanceId(), "existing-instance");

    ASSERT_TRUE(runtime.stop(::std::chrono::seconds(1)).has_value());
}

// ServiceGateway

TEST(CatalogServiceGateway, InstanceVersionAcceptsNumericForm)
{
    auto transport { ::std::make_unique<FakeHttpTransport>() };
    transport->handler = [](const ::std::string&, const ::std::string& url, const ::std::string&) -> HttpResponseData
    {
        HttpResponseData response {};
        response.transport_ok = true;
        response.status       = 200;
        if (url.find("healthyOnly=true") != ::std::string::npos)
        {
            // 回归: 服务端 catalog 3.x 返回数字型 version (如 0), 旧实现只接受字符串会直接判协议错误
            response.body =
                R"([{"id":"i1","ip":"10.0.0.1","healthy":true,"enabled":true,"version":0,"endpoints":[{"name":"http","protocol":"http","port":8080}]}])";
        }
        else
        {
            response.body = "{}";
        }
        return response;
    };

    ServiceGateway gateway { "http://127.0.0.1:18081", ::std::move(transport), ::std::chrono::milliseconds { 500 } };

    ServiceQuery   query {};
    query.service_id = "svc-a";
    const auto result { gateway.resolve(query) };
    ASSERT_TRUE(result.has_value()) << result.error().message;
    ASSERT_EQ(result.value().endpoints.size(), 1u);
    EXPECT_EQ(result.value().endpoints[0].version, "0");
}

TEST(CatalogServiceGateway, ResolveFiltersUnhealthyAndDisabled)
{
    auto transport { ::std::make_unique<FakeHttpTransport>() };
    transport->handler = [](const ::std::string&, const ::std::string& url, const ::std::string&) -> HttpResponseData
    {
        HttpResponseData response {};
        response.transport_ok = true;
        response.status       = 200;
        if (url.find("healthyOnly=true") != ::std::string::npos)
        {
            response.body = R"([
				{"id":"i1","ip":"10.0.0.1","healthy":true,"enabled":true,"endpoints":[{"name":"http","protocol":"http","port":8080}]},
				{"id":"i2","ip":"10.0.0.2","healthy":false,"endpoints":[{"name":"http","protocol":"http","port":8080}]},
				{"id":"i3","ip":"10.0.0.3","healthy":true,"enabled":false,"endpoints":[{"name":"http","protocol":"http","port":8080}]}
			])";
        }
        else
        {
            response.body = "{}";
        }
        return response;
    };

    ServiceGateway gateway { "http://127.0.0.1:18081", ::std::move(transport), ::std::chrono::milliseconds { 500 } };

    ServiceQuery   query {};
    query.service_id = "svc-a";
    const auto result { gateway.resolve(query) };
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value().endpoints.size(), 1u);
    EXPECT_EQ(result.value().endpoints[0].instance_id, "i1");
}

TEST(CatalogServiceGateway, InstanceStatusHealthyIsCaseInsensitive)
{
    auto transport { ::std::make_unique<FakeHttpTransport>() };
    transport->handler = [](const ::std::string&, const ::std::string&, const ::std::string&) -> HttpResponseData
    {
        HttpResponseData response {};
        response.transport_ok = true;
        response.status       = 200;
        response.body         = R"({"overallStatus":"up","message":"ok","components":{}})";
        return response;
    };

    ServiceGateway gateway { "http://127.0.0.1:18081", ::std::move(transport), ::std::chrono::milliseconds { 500 } };

    const auto     result { gateway.getInstanceStatus("public", "DEFAULT_GROUP", "svc-a", "i1") };
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result.value().healthy);
}

// 组播节点公告解析 (catalog-node-announce-v1)

namespace
{
    // 实测报文 (192.168.1.118 每 ~5s 发布一次)
    const char* const kMulticastSample = R"({"type":"catalog-node-announce-v1","timestamp":1789640712667,)"
                                         R"("node":{"localName":"指挥所118","deploymentLocation":"未配置","nodePurpose":"服务目录节点",)"
                                         R"("department":"未配置","accessAddress":"http://192.168.1.118:30906","onlineServiceCount":3}})";
} // namespace

TEST(CatalogMulticastAnnouncement, ParsesRealAnnouncementAndSplitsAccessAddress)
{
    const auto result { UdpMulticastAnnouncementListener::parse(kMulticastSample) };
    ASSERT_TRUE(result.has_value()) << result.error().message;

    EXPECT_EQ(result.value().type, "catalog-node-announce-v1");
    EXPECT_EQ(result.value().timestamp, 1'789'640'712'667ll);
    EXPECT_EQ(result.value().local_name, "指挥所118");
    EXPECT_EQ(result.value().node_purpose, "服务目录节点");
    EXPECT_EQ(result.value().access_address, "http://192.168.1.118:30906");
    EXPECT_EQ(result.value().online_service_count, 3);
    // accessAddress 解析出的 ip / port
    EXPECT_EQ(result.value().ip, "192.168.1.118");
    EXPECT_EQ(result.value().http_port, 30'906);
}

TEST(CatalogMulticastAnnouncement, IgnoresOtherTypesAndMalformedPayloads)
{
    const auto is_protocol_error = [](const ::std::string& payload)
    {
        const auto result { UdpMulticastAnnouncementListener::parse(payload) };
        return !result.has_value() && result.error().code == CatalogError::PROTOCOL_ERROR;
    };

    // type 不匹配 (组播上还有其它协议/版本)
    EXPECT_TRUE(is_protocol_error(R"({"type":"catalog-node-announce-v2","node":{}})"));
    // 非 JSON
    EXPECT_TRUE(is_protocol_error("RTPS-binary-payload"));
    // 缺 node
    EXPECT_TRUE(is_protocol_error(R"({"type":"catalog-node-announce-v1"})"));
    // 缺 accessAddress
    EXPECT_TRUE(is_protocol_error(R"({"type":"catalog-node-announce-v1","node":{"localName":"x"}})"));
}

TEST(CatalogMulticastAnnouncement, ToleratesAddressWithoutSchemeOrPort)
{
    // 无 scheme, 带端口
    const auto with_port {
        UdpMulticastAnnouncementListener::parse(R"({"type":"catalog-node-announce-v1","node":{"accessAddress":"10.0.0.5:8080"}})")
    };
    ASSERT_TRUE(with_port.has_value());
    EXPECT_EQ(with_port.value().ip, "10.0.0.5");
    EXPECT_EQ(with_port.value().http_port, 8080);

    // 无 scheme 无端口 -> 端口回落 0
    const auto no_port { UdpMulticastAnnouncementListener::parse(R"({"type":"catalog-node-announce-v1","node":{"accessAddress":"10.0.0.5"}})") };
    ASSERT_TRUE(no_port.has_value());
    EXPECT_EQ(no_port.value().ip, "10.0.0.5");
    EXPECT_EQ(no_port.value().http_port, 0);

    // 带路径
    const auto with_path {
        UdpMulticastAnnouncementListener::parse(R"({"type":"catalog-node-announce-v1","node":{"accessAddress":"http://10.0.0.7:30906/base"}})")
    };
    ASSERT_TRUE(with_path.has_value());
    EXPECT_EQ(with_path.value().ip, "10.0.0.7");
    EXPECT_EQ(with_path.value().http_port, 30'906);
}

// 数据池读取 (通过 key)

TEST(CatalogServiceGateway, GetDataValueDecodesPayloadAndMapsNotFound)
{
    auto transport { ::std::make_unique<FakeHttpTransport>() };
    transport->handler = [](const ::std::string&, const ::std::string& url, const ::std::string&) -> HttpResponseData
    {
        HttpResponseData response {};
        response.transport_ok = true;
        if (url.find("missing") != ::std::string::npos)
        {
            response.status = 404;
            response.body   = R"({"code":"NOT_FOUND"})";
            return response;
        }
        response.status = 200;
        response.body   = R"({"key":"nodeList","contentType":"application/json","version":3,)"
                          R"("sourceNodeId":"NODE-A","updatedAt":"2026-09-23T12:00:00Z",)"
                          R"("payloadBase64":"aGVsbG8=","payloadBytes":5})";
        return response;
    };

    ServiceGateway gateway { "http://127.0.0.1:18081", ::std::move(transport), ::std::chrono::milliseconds { 500 } };

    const auto     value { gateway.getDataValue("nodeList") };
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value.value().key, "nodeList");
    EXPECT_EQ(value.value().content_type, "application/json");
    EXPECT_EQ(value.value().version, 3);
    EXPECT_EQ(value.value().source_node_id, "NODE-A");
    // 注意: 花括号初始化含逗号, 直接塞进 EXPECT_EQ 会被预处理器拆成多个实参, 故先落变量
    const ::std::string payload_text { value.value().payload.begin(), value.value().payload.end() };
    EXPECT_EQ(payload_text, "hello");

    // 空 key 为参数错误, 不发起请求
    const auto empty_key { gateway.getDataValue("") };
    ASSERT_FALSE(empty_key.has_value());
    EXPECT_EQ(empty_key.error().code, CatalogError::INVALID_ARGUMENT);

    // 404 -> DATA_NOT_FOUND (不可重试)
    const auto missing { gateway.getDataValue("missing") };
    ASSERT_FALSE(missing.has_value());
    EXPECT_EQ(missing.error().code, CatalogError::DATA_NOT_FOUND);
    EXPECT_FALSE(missing.error().retryable);
}

TEST(CatalogServiceGateway, GetDataValueRejectsPayloadSizeMismatch)
{
    auto transport { ::std::make_unique<FakeHttpTransport>() };
    transport->handler = [](const ::std::string&, const ::std::string&, const ::std::string&) -> HttpResponseData
    {
        HttpResponseData response {};
        response.transport_ok = true;
        response.status       = 200;
        // payloadBytes 声称 9 字节, 实际解码为 5 字节
        response.body = R"({"key":"nodeList","contentType":"application/json","version":1,)"
                        R"("sourceNodeId":"NODE-A","updatedAt":"t","payloadBase64":"aGVsbG8=","payloadBytes":9})";
        return response;
    };

    ServiceGateway gateway { "http://127.0.0.1:18081", ::std::move(transport), ::std::chrono::milliseconds { 500 } };

    const auto     value { gateway.getDataValue("nodeList") };
    ASSERT_FALSE(value.has_value());
    EXPECT_EQ(value.error().code, CatalogError::PROTOCOL_ERROR);
}

// 节点清单 (结构化行)

TEST(CatalogServiceGateway, GetNodeListParsesStructuredRows)
{
    auto transport { ::std::make_unique<FakeHttpTransport>() };
    transport->handler = [](const ::std::string&, const ::std::string& url, const ::std::string&) -> HttpResponseData
    {
        HttpResponseData response {};
        response.transport_ok = true;
        response.status       = 200;
        if (url.find("/api/datapool/v1/discovery/node-list") == ::std::string::npos)
        {
            response.body = "{}";
            return response;
        }
        response.body = R"({"key":"nodeList","value":"{\"localNodeId\":\"NODE-A\",\"nodes\":[)"
                        R"({\"node\":{\"nodeId\":\"NODE-B\",\"nodeName\":\"\u8282\u70b9B\"},)"
                        R"(\"address\":\"192.168.1.118:30906\",\"relation\":\"peer\",)"
                        R"(\"configVersion\":{\"classificationRevision\":3,\"authorizationRevision\":4},)"
                        R"(\"effectivePermissions\":[\"read.node\"],\"mqtt\":\"tcp://192.168.1.118:1883\",)"
                        R"(\"status\":{\"online\":true,\"label\":\"\u5c31\u7eea\",\"responseMillis\":12,\"missedScans\":0},)"
                        R"(\"authorization\":{\"networkRevision\":7,\"ownerNodeId\":\"NODE-A\",)"
                        R"(\"pairs\":[{\"otherId\":\"NODE-B\",\"otherName\":\"\u8282\u70b9B\",\"view\":\"open\",\"otherView\":\"open\"}],)"
                        R"(\"grants\":[{\"peerNodeId\":\"NODE-B\",\"peerNodeName\":\"\u8282\u70b9B\",\"view\":\"open\",)"
                        R"(\"otherView\":\"open\",\"outboundOperations\":[\"a\"],\"inboundOperations\":[\"b\"]}]}}]}"})";
        return response;
    };

    ServiceGateway gateway { "http://127.0.0.1:18081", ::std::move(transport), ::std::chrono::milliseconds { 500 } };

    const auto     list { gateway.getNodeList() };
    ASSERT_TRUE(list.has_value()) << list.error().message;
    EXPECT_EQ(list.value().local_node_id, "NODE-A");
    ASSERT_EQ(list.value().nodes.size(), 1u);

    const auto& entry { list.value().nodes[0] };
    EXPECT_EQ(entry.node_id, "NODE-B");
    EXPECT_EQ(entry.node_name, "节点B");
    EXPECT_EQ(entry.address, "192.168.1.118:30906");
    EXPECT_EQ(entry.relation, "peer");
    EXPECT_EQ(entry.config_version.classification_revision, 3);
    EXPECT_EQ(entry.config_version.authorization_revision, 4);
    ASSERT_EQ(entry.effective_permissions.size(), 1u);
    EXPECT_EQ(entry.effective_permissions[0], "read.node");
    EXPECT_EQ(entry.mqtt, "tcp://192.168.1.118:1883");
    EXPECT_TRUE(entry.status.online);
    EXPECT_EQ(entry.status.label, "就绪");
    EXPECT_EQ(entry.status.response_millis, 12);
    EXPECT_EQ(entry.status.missed_scans, 0);
    EXPECT_EQ(entry.authorization.network_revision, 7);
    EXPECT_EQ(entry.authorization.owner_node_id, "NODE-A");
    ASSERT_EQ(entry.authorization.pairs.size(), 1u);
    EXPECT_EQ(entry.authorization.pairs[0].other_id, "NODE-B");
    EXPECT_EQ(entry.authorization.pairs[0].other_view, "open");
    ASSERT_EQ(entry.authorization.grants.size(), 1u);
    ASSERT_EQ(entry.authorization.grants[0].outbound_operations.size(), 1u);
    EXPECT_EQ(entry.authorization.grants[0].inbound_operations[0], "b");
}

TEST(CatalogServiceGateway, GetNodeListRejectsUnexpectedKey)
{
    auto transport { ::std::make_unique<FakeHttpTransport>() };
    transport->handler = [](const ::std::string&, const ::std::string&, const ::std::string&) -> HttpResponseData
    {
        HttpResponseData response {};
        response.transport_ok = true;
        response.status       = 200;
        response.body         = R"({"key":"somethingElse","value":"{}"})";
        return response;
    };

    ServiceGateway gateway { "http://127.0.0.1:18081", ::std::move(transport), ::std::chrono::milliseconds { 500 } };

    const auto     list { gateway.getNodeList() };
    ASSERT_FALSE(list.has_value());
    EXPECT_EQ(list.error().code, CatalogError::PROTOCOL_ERROR);
}

// UdpAnnouncementListener

TEST(CatalogAnnouncementListener, ReceivesLoopbackAnnouncement)
{
    // 选一个空闲 UDP 端口 (绑 0 获取后立即释放, 小竞态可接受)
    int port { 0 };
    {
        ::asio::io_context      io {};
        ::asio::ip::udp::socket probe { io };
        probe.open(::asio::ip::udp::v4());
        probe.bind(::asio::ip::udp::endpoint { ::asio::ip::udp::v4(), 0 });
        port = probe.local_endpoint().port();
        probe.close();
    }

    ::std::mutex                        mutex {};
    ::std::condition_variable           cv {};
    bool                                got { false };
    plane::catalog::CatalogAnnouncement announcement {};

    UdpAnnouncementListener             listener { port,
                                                   "127.0.0.1",
                                                   [&](const plane::catalog::CatalogAnnouncement& value)
                                                   {
                                           ::std::lock_guard<::std::mutex> lock { mutex };
                                           announcement = value;
                                           got          = true;
                                           cv.notify_all();
                                       } };
    ASSERT_TRUE(listener.start().has_value());

    // 发送一个 command=0x82 公告包
    {
        ::asio::io_context      io {};
        ::asio::ip::udp::socket sender { io };
        sender.open(::asio::ip::udp::v4());

        plane::catalog::internal::ProbePacket packet {};
        packet.command     = plane::catalog::internal::PROBE_ANNOUNCE_COMMAND;
        packet.node_id     = "ann-node";
        packet.node_name   = "catalog-ann";
        packet.instance_id = "inst-ann";
        packet.http_port   = 8081;

        const auto encoded { plane::catalog::internal::encodeProbePacket(packet) };
        ASSERT_TRUE(encoded.has_value());
        const ::asio::ip::udp::endpoint target { ::asio::ip::make_address("127.0.0.1"), static_cast<unsigned short>(port) };
        sender.send_to(::asio::buffer(encoded.value()), target);
    }

    {
        ::std::unique_lock<::std::mutex> lock { mutex };
        (void)cv.wait_for(
            lock,
            ::std::chrono::seconds(2),
            [&]()
            {
                return got;
            }
        );
    }
    listener.stop();

    ASSERT_TRUE(got) << "2s 内未收到公告回调";
    EXPECT_EQ(announcement.node_id, "ann-node");
    EXPECT_EQ(announcement.ip, "127.0.0.1");
    EXPECT_EQ(announcement.http_port, 8081);
}

// CppHttpTransport (端到端, 本地回环服务)

TEST(CppHttpTransportTest, GetRoundTripAndQueryParsing)
{
    using plane::catalog::internal::CppHttpTransport;

    ::httplib::Server server {};
    server.Get(
        "/ping",
        [](const ::httplib::Request&, ::httplib::Response& res)
        {
            res.set_content("pong", "text/plain");
        }
    );
    server.Get(
        "/echo",
        [](const ::httplib::Request& request, ::httplib::Response& res)
        {
            // 回显原始请求目标, 验证 path + query 拼接完整到达服务端
            res.set_content(request.target, "text/plain");
        }
    );

    const int port { server.bind_to_any_port("127.0.0.1") };
    ASSERT_GT(port, 0) << "无法绑定本地测试端口";
    ::std::thread server_thread { [&server]()
                                  {
                                      (void)server.listen_after_bind();
                                  } };
    server.wait_until_ready();

    CppHttpTransport transport {};
    transport.setTimeout(::std::chrono::milliseconds { 2000 });

    const ::std::string base { "http://127.0.0.1:" + ::std::to_string(port) };
    const auto          ping { transport.get(base + "/ping") };
    ASSERT_TRUE(ping.transport_ok) << ping.transport_error;
    EXPECT_EQ(ping.status, 200);
    EXPECT_EQ(ping.body, "pong");

    const auto echo { transport.get(base + "/echo?a=1&b=2") };
    ASSERT_TRUE(echo.transport_ok) << echo.transport_error;
    EXPECT_EQ(echo.status, 200);
    EXPECT_EQ(echo.body, "/echo?a=1&b=2");

    // 非法/不支持 URL: 不发起连接, 返回传输错误 (URL 解析由 cpp-httplib 完成)
    const auto bad_scheme { transport.get("ftp://127.0.0.1/x") };
    EXPECT_FALSE(bad_scheme.transport_ok);
    EXPECT_FALSE(bad_scheme.transport_error.empty());

    const auto https_url { transport.get("https://127.0.0.1/x") };
    EXPECT_FALSE(https_url.transport_ok);

    server.stop();
    server_thread.join();
}

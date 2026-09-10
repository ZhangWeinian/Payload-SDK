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
#include "manager/catalog/client/internal/service/ServiceGateway.h"
#include "manager/catalog/client/internal/transport/CppHttpTransport.h"
#include "manager/catalog/client/internal/transport/HttpTransport.h"

namespace
{
	using plane::catalog::CatalogEndpoint;
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

	using Clock = _STD_CHRONO steady_clock;

	// ---- 注入测试替身 ----

	class FakeHttpTransport final: public HttpTransport
	{
	public:
		// handler: (method, url, body) -> response; 为空时统一返回 200 {"id":"test-instance"}
		_STD function<HttpResponseData(const _STD string&, const _STD string&, const _STD string&)> handler {};

		HttpResponseData																			get(const _STD string& url) override
		{
			return this->dispatch("GET", url, "");
		}

		HttpResponseData post(const _STD string& url, const _STD string& body) override
		{
			return this->dispatch("POST", url, body);
		}

		HttpResponseData put(const _STD string& url, const _STD string& body) override
		{
			return this->dispatch("PUT", url, body);
		}

		HttpResponseData del(const _STD string& url) override
		{
			return this->dispatch("DELETE", url, "");
		}

		void				   setTimeout(_STD_CHRONO milliseconds) override {}

		_NODISCARD _STD size_t calls(void) const
		{
			_STD lock_guard<_STD mutex> lock { this->mutex };
			return this->count;
		}

		_NODISCARD bool sawCall(const _STD string& method, const _STD string& url_fragment) const
		{
			_STD lock_guard<_STD mutex> lock { this->mutex };
			for (const auto& call : this->log)
			{
				if (call.rfind(method + " ", 0) == 0 && call.find(url_fragment) != _STD string::npos)
				{
					return true;
				}
			}
			return false;
		}

	private:
		HttpResponseData dispatch(const _STD string& method, const _STD string& url, const _STD string& body)
		{
			{
				_STD lock_guard<_STD mutex> lock { this->mutex };
				++this->count;
				this->log.push_back(method + " " + url);
			}
			if (this->handler)
			{
				return this->handler(method, url, body);
			}
			return okJson(R"({"id":"test-instance"})");
		}

		static HttpResponseData okJson(const _STD string& body)
		{
			HttpResponseData response {};
			response.transport_ok = true;
			response.status		  = 200;
			response.body		  = body;
			return response;
		}

		mutable _STD mutex mutex {};
		_STD size_t		   count { 0 };
		_STD vector<_STD string> log {};
	};

	class FakeDiscoveryClient final: public DiscoveryClient
	{
	public:
		DiscoveryReport discover(const DiscoveryConfig&, const _STD atomic<bool>&) override
		{
			DiscoveryReport report {};
			report.status = DiscoveryStatus::OK;
			report.endpoints.push_back(this->endpoint);
			return report;
		}

		void			saveSuccessfulIp(const _STD string&) override {}

		CatalogEndpoint endpoint {};
	};

	_NODISCARD bool waitForState(const CatalogRuntime& runtime, CatalogState expected, _STD_CHRONO milliseconds timeout)
	{
		const auto deadline { Clock::now() + timeout };
		while (Clock::now() < deadline)
		{
			if (runtime.state() == expected)
			{
				return true;
			}
			_STD this_thread::sleep_for(_STD_CHRONO milliseconds { 10 });
		}
		return runtime.state() == expected;
	}

	struct RuntimeDeps
	{
		_STD unique_ptr<FakeHttpTransport> transport {};
		FakeHttpTransport*				   transport_raw { nullptr };
		_STD unique_ptr<FakeDiscoveryClient> discovery {};
	};

	_NODISCARD RuntimeDeps makeDeps(void)
	{
		RuntimeDeps			  deps {};
		deps.transport						 = _STD make_unique<FakeHttpTransport>();
		deps.transport_raw					 = deps.transport.get();
		deps.discovery						 = _STD make_unique<FakeDiscoveryClient>();
		deps.discovery->endpoint.instance_id = "catalog-1";
		deps.discovery->endpoint.ip			 = "127.0.0.1";
		deps.discovery->endpoint.http_port	 = 18'081;
		deps.discovery->endpoint.node_name	 = "test-catalog";
		return deps;
	}
} // namespace

// ---- CatalogRuntime 注入与注册主链 ----

TEST(CatalogRuntimeInjection, UsesInjectedTransportAndReachesReady)
{
	auto				  deps { makeDeps() };

	CatalogRuntimeOptions options {};
	options.registration.service_id	  = "swarm.test.device";
	options.registration.service_name = "测试设备";
	options.registration.version	  = "1.0.0";

	DiscoveryConfig discovery_config {};
	discovery_config.node_id = "TESTNODE";
	discovery_config.port	 = 30'906;
	discovery_config.targets = { "127.0.0.1" };

	CatalogRuntime runtime { options, discovery_config, _STD move(deps.transport), _STD move(deps.discovery) };

	ASSERT_TRUE(runtime.start().isOk());
	ASSERT_TRUE(runtime.registerServiceInstance().isOk());
	ASSERT_TRUE(waitForState(runtime, CatalogState::READY, _STD_CHRONO seconds(3)));

	// 注入的 transport 必须真实收到注册请求 (回归: 注入不被丢弃)
	EXPECT_TRUE(deps.transport_raw->sawCall("POST", "/api/registry/services"));
	EXPECT_GT(deps.transport_raw->calls(), 0u);
	EXPECT_EQ(runtime.instanceId(), "test-instance");

	ASSERT_TRUE(runtime.stop(_STD_CHRONO seconds(1)).isOk());
}

TEST(CatalogRuntimeInjection, IdempotentRegisterConflictIsAccepted)
{
	auto deps { makeDeps() };
	deps.transport_raw->handler = [](const _STD string& method, const _STD string& url, const _STD string&) -> HttpResponseData
	{
		HttpResponseData response {};
		response.transport_ok = true;
		if (method == "POST" && url.find("/instances") != _STD string::npos && url.find("/heartbeat") == _STD string::npos)
		{
			// 服务端幂等: 409 且携带 id -> 视为注册成功
			response.status = 409;
			response.body	= R"({"code":"ALREADY_REGISTERED","id":"existing-instance"})";
			return response;
		}
		response.status = 200;
		response.body	= R"({"id":"existing-instance"})";
		return response;
	};

	CatalogRuntimeOptions options {};
	options.registration.service_id	  = "swarm.test.device";
	options.registration.service_name = "测试设备";
	options.registration.version	  = "1.0.0";

	DiscoveryConfig discovery_config {};
	discovery_config.node_id = "TESTNODE";
	discovery_config.port	 = 30'906;
	discovery_config.targets = { "127.0.0.1" };

	CatalogRuntime runtime { options, discovery_config, _STD move(deps.transport), _STD move(deps.discovery) };

	ASSERT_TRUE(runtime.start().isOk());
	ASSERT_TRUE(runtime.registerServiceInstance().isOk());
	ASSERT_TRUE(waitForState(runtime, CatalogState::READY, _STD_CHRONO seconds(3)));
	EXPECT_EQ(runtime.instanceId(), "existing-instance");

	ASSERT_TRUE(runtime.stop(_STD_CHRONO seconds(1)).isOk());
}

// ---- ServiceGateway ----

TEST(CatalogServiceGateway, ResolveFiltersUnhealthyAndDisabled)
{
	auto transport { _STD make_unique<FakeHttpTransport>() };
	transport->handler = [](const _STD string&, const _STD string& url, const _STD string&) -> HttpResponseData
	{
		HttpResponseData response {};
		response.transport_ok = true;
		response.status		  = 200;
		if (url.find("healthyOnly=true") != _STD string::npos)
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

	ServiceGateway gateway { "http://127.0.0.1:18081", _STD move(transport), _STD_CHRONO milliseconds { 500 } };

	ServiceQuery   query {};
	query.service_id = "svc-a";
	const auto result { gateway.resolve(query) };
	ASSERT_TRUE(result.isOk());
	ASSERT_EQ(result.value().endpoints.size(), 1u);
	EXPECT_EQ(result.value().endpoints[0].instance_id, "i1");
}

TEST(CatalogServiceGateway, InstanceStatusHealthyIsCaseInsensitive)
{
	auto transport { _STD make_unique<FakeHttpTransport>() };
	transport->handler = [](const _STD string&, const _STD string&, const _STD string&) -> HttpResponseData
	{
		HttpResponseData response {};
		response.transport_ok = true;
		response.status		  = 200;
		response.body		  = R"({"overallStatus":"up","message":"ok","components":{}})";
		return response;
	};

	ServiceGateway gateway { "http://127.0.0.1:18081", _STD move(transport), _STD_CHRONO milliseconds { 500 } };

	const auto	   result { gateway.getInstanceStatus("public", "DEFAULT_GROUP", "svc-a", "i1") };
	ASSERT_TRUE(result.isOk());
	EXPECT_TRUE(result.value().healthy);
}

// ---- UdpAnnouncementListener ----

TEST(CatalogAnnouncementListener, ReceivesLoopbackAnnouncement)
{
	// 选一个空闲 UDP 端口 (绑 0 获取后立即释放, 小竞态可接受)
	int port { 0 };
	{
		_ASIO io_context io {};
		_ASIO ip::udp::socket probe { io };
		probe.open(_ASIO ip::udp::v4());
		probe.bind(_ASIO ip::udp::endpoint { _ASIO ip::udp::v4(), 0 });
		port = probe.local_endpoint().port();
		probe.close();
	}

	_STD mutex							mutex {};
	_STD condition_variable				cv {};
	bool								got { false };
	plane::catalog::CatalogAnnouncement announcement {};

	UdpAnnouncementListener				listener { port,
												   "127.0.0.1",
												   [&](const plane::catalog::CatalogAnnouncement& value)
												   {
										   _STD lock_guard<_STD mutex> lock { mutex };
										   announcement = value;
										   got			= true;
										   cv.notify_all();
									   } };
	ASSERT_TRUE(listener.start().isOk());

	// 发送一个 command=0x82 公告包
	{
		_ASIO io_context io {};
		_ASIO ip::udp::socket sender { io };
		sender.open(_ASIO ip::udp::v4());

		plane::catalog::internal::ProbePacket packet {};
		packet.command	   = plane::catalog::internal::PROBE_ANNOUNCE_COMMAND;
		packet.node_id	   = "ann-node";
		packet.node_name   = "catalog-ann";
		packet.instance_id = "inst-ann";
		packet.http_port   = 8081;

		const auto encoded { plane::catalog::internal::encodeProbePacket(packet) };
		ASSERT_TRUE(encoded.isOk());
		const _ASIO ip::udp::endpoint target { _ASIO ip::make_address("127.0.0.1"), static_cast<unsigned short>(port) };
		sender.send_to(_ASIO buffer(encoded.value()), target);
	}

	{
		_STD unique_lock<_STD mutex> lock { mutex };
		(void)cv.wait_for(
			lock,
			_STD_CHRONO seconds(2),
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

// ---- CppHttpTransport (端到端, 本地回环服务) ----

TEST(CppHttpTransportTest, GetRoundTripAndQueryParsing)
{
	using plane::catalog::internal::CppHttpTransport;

	_HTTPLIB Server server {};
	server.Get(
		"/ping",
		[](const _HTTPLIB Request&, _HTTPLIB Response& res)
		{
			res.set_content("pong", "text/plain");
		}
	);
	server.Get(
		"/echo",
		[](const _HTTPLIB Request& request, _HTTPLIB Response& res)
		{
			// 回显原始请求目标, 验证 path + query 拼接完整到达服务端
			res.set_content(request.target, "text/plain");
		}
	);

	const int port { server.bind_to_any_port("127.0.0.1") };
	ASSERT_GT(port, 0) << "无法绑定本地测试端口";
	_STD thread server_thread { [&server]()
								{
									(void)server.listen_after_bind();
								} };
	server.wait_until_ready();

	CppHttpTransport transport {};
	transport.setTimeout(_STD_CHRONO milliseconds { 2000 });

	const _STD string base { "http://127.0.0.1:" + _STD to_string(port) };
	const auto		  ping { transport.get(base + "/ping") };
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

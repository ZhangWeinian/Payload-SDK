// cy_psdk/manager/websocket/WsClient.cpp
//
// WebSocket 数据订阅实现 (boost::beast + boost::asio 异步 IO 链)。
// 连接/订阅/保活/重连策略与常量对齐 msdk WebSocketRepository 与 GlobalHttpClient
// (OkHttp: connectTimeout=10s, readTimeout=0, pingInterval=15s; 重连间隔默认 2s)。
//
// IO 链: 等待目录就绪 -> TCP 连接 -> WS 握手 -> 发送订阅 -> 读循环 (+ 周期 ping 保活);
//        任一环节失败 -> 清理 -> 固定间隔重连 (手动 stop 后不再重连)。

#include "manager/websocket/WsClient.h"

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

#include <fmt/format.h>

#include <string_view>
#include <array>
#include <chrono>
#include <exception>
#include <string>
#include <thread>

#include "manager/catalog/CatalogManager.h"
#include "manager/plane_state/PlaneStateStore.h"
#include "utils/log_util/Logger.h"

namespace plane::manager
{
	namespace
	{
		namespace asio	= boost::asio;
		namespace beast = boost::beast;

		using WsStream	= beast::websocket::stream<beast::tcp_stream>;
		using WsError	= beast::error_code;

		// --- 协议与策略常量 (对齐 msdk WebSocketRepository / GlobalHttpClient / AppConfigEntity) ---
		constexpr _STD uint16_t kServerPort { 8888 };							 // msdk: "ws://${registryIp}:8888"
		constexpr auto			kConnectTimeout { _STD_CHRONO seconds(10) };	 // OkHttp connectTimeout=10s (含 WS 握手)
		constexpr auto			kPingInterval { _STD_CHRONO seconds(15) };		 // OkHttp pingInterval=15s, 短于服务端空闲超时
		constexpr auto			kPingTimeout { _STD_CHRONO seconds(10) };		 // 发出 ping 后等待 pong 的时限, 超时判定死链
		constexpr auto			kReconnectInterval { _STD_CHRONO seconds(2) };	 // serviceReconnectIntervalS 默认值
		constexpr auto			kCatalogWaitInterval { _STD_CHRONO seconds(2) }; // 等待目录就绪的轮询间隔
		constexpr _STD size_t	kPayloadLogLimit { 1024 };						 // 原始数据日志截断上限 (字节)

		// 连接成功后立即订阅的数据类型 (msdk 默认订阅集)
		_STD vector<_STD string> subscribeTypes()
		{
			return { "stationSwarmState", "event", "stationTaskStatus" };
		}
	} // namespace

	struct WsClient::Impl
	{
		// --- 运行时资源 (除原子标志外仅 io 线程访问) ---
		asio::io_context ioc { 1 };
		_STD unique_ptr<WsStream> ws {};					  // 每轮连接新建; 关闭后保留至下一轮覆盖
		asio::steady_timer		  ping_timer { ioc };		  // 周期 ping
		asio::steady_timer		  ping_timeout_timer { ioc }; // pong 等待超时
		asio::steady_timer		  retry_timer { ioc };		  // 目录等待/断线重连
		beast::flat_buffer		  rx_buffer {};				  // 接收缓冲
		_STD string				  host {};					  // 当前连接 IP (握手 Host/日志)
		_STD string				  subscribe_payload {};		  // 订阅报文 (发送期间保持存活)
		_STD thread				  thread {};
		_STD atomic<bool> running { false };				  // 期望运行 (stop 后为 false)
		_STD atomic<bool> conn_active { false };			  // 本轮连接仍在进行 (含关闭中)
		_STD atomic<bool> connected { false };				  // 已完成握手且未断开
		bool			  failure_logged { false };			  // 连接失败日志降噪 (恢复后重置)
		bool			  wait_logged { false };			  // 等待目录/地址日志降噪 (拿到地址后重置)

		void			  start();
		void			  stop();

		void			  scheduleConnect();
		void			  doConnect(const _STD string& ip);
		void			  doSubscribe();
		void			  startRead();
		void			  schedulePing();
		void			  doPing();

		void			  onMessage();

		void			  onIoError(const char* stage, const WsError& ec);
		void			  teardown(const _STD string& reason);
		void			  closeTransport();
		void			  scheduleReconnect();
	};

	// ============================ 生命周期 ============================

	void WsClient::Impl::start()
	{
		this->running.store(true, _STD memory_order_release);
		this->ioc.restart(); // 支持 stop 后再次 start (此时无线程在 run)
		this->thread = _STD thread(
			[this]
			{
				this->ioc.run();
			}
		);
		asio::post(
			this->ioc,
			[this]
			{
				this->scheduleConnect();
			}
		);
	}

	void WsClient::Impl::stop()
	{
		this->running.store(false, _STD memory_order_release);

		// 清理任务投递到 io 线程: 关闭连接/取消定时器后 run() 无未决工作, 自然返回
		asio::post(
			this->ioc,
			[this]
			{
				this->conn_active.store(false, _STD memory_order_release);
				this->connected.store(false, _STD memory_order_release);
				this->closeTransport();
				this->ws.reset();
			}
		);

		if (this->thread.joinable())
		{
			this->thread.join();
		}

		// 同步域模型: 停止后明确置为"未连接" (手动停止时 teardown 回调可能不触发,
		// 否则状态板会在退出阶段残留"已连接")
		plane::domain::PlaneStateStore::getInstance().update(
			[](plane::domain::PlaneStateDataClass& st)
			{
				st.web_socket_connected = false;
			}
		);
	}

	// ============================ 连接链 ============================

	void WsClient::Impl::scheduleConnect()
	{
		if (!this->running.load(_STD memory_order_acquire))
		{
			return;
		}

		// 目录未就绪/未发现服务端地址: 稍后重试 (对齐 msdk "registryIp 就绪后才连接")
		_STD string ip {};
		auto&		catalog { CatalogManager::getInstance() };
		if (catalog.isCatalogReady())
		{
			ip = catalog.getCatalogServerIp();
		}

		if (ip.empty())
		{
			if (!this->wait_logged)
			{
				LOG_WARN(
					"WebSocket 等待目录就绪/服务端地址 (catalog_ready={}), 每 {}s 重试",
					catalog.isCatalogReady(),
					kCatalogWaitInterval.count()
				);
				this->wait_logged = true;
			}
			this->retry_timer.expires_after(kCatalogWaitInterval);
			this->retry_timer.async_wait(
				[this](const WsError& ec)
				{
					if (!ec)
					{
						this->scheduleConnect();
					}
				}
			);
			return;
		}

		this->wait_logged = false; // 已取到地址: 若再进入等待则重新提醒
		LOG_DEBUG("WebSocket 连接目标: {}:{}", ip, kServerPort);
		this->doConnect(ip);
	}

	void WsClient::Impl::doConnect(const _STD string& ip)
	{
		if (!this->running.load(_STD memory_order_acquire))
		{
			return;
		}

		beast::error_code addr_ec {};
		const auto		  address { asio::ip::make_address(ip, addr_ec) };
		if (addr_ec)
		{
			LOG_WARN("WebSocket 目录服务端 IP 非法: '{}'", ip);
			this->scheduleReconnect();
			return;
		}

		this->host = ip;
		this->ws   = _STD make_unique<WsStream>(this->ioc);
		this->rx_buffer.consume(this->rx_buffer.size()); // 清理上一轮残留
		this->conn_active.store(true, _STD memory_order_release);

		auto& lowest { beast::get_lowest_layer(*this->ws) };
		lowest.expires_after(kConnectTimeout); // 连接 + 握手共用超时窗口

		// Beast 的 async_connect 只接受 endpoint 序列, 单 IP 场景包装为单元素数组;
		// 连接与握手的超时由上方 expires_after 统一控制。
		_STD array<asio::ip::tcp::endpoint, 1> endpoints {
			asio::ip::tcp::endpoint { address, kServerPort }
		};
		lowest.async_connect(
			endpoints,
			[this](const WsError& ec, const asio::ip::tcp::endpoint&)
			{
				if (ec)
				{
					this->onIoError("TCP 连接", ec);
					return;
				}
				if (!this->conn_active.load(_STD memory_order_acquire))
				{
					return;
				}

				// HTTP Upgrade 握手 (Host 头需含端口)
				this->ws->async_handshake(
					_FMT format("{}:{}", this->host, kServerPort),
					"/",
					[this](const WsError& hs_ec)
					{
						if (hs_ec)
						{
							this->onIoError("WebSocket 握手", hs_ec);
							return;
						}
						if (!this->conn_active.load(_STD memory_order_acquire))
						{
							return;
						}

						beast::get_lowest_layer(*this->ws).expires_never(); // 长连接不设读写超时 (OkHttp readTimeout=0)
						this->connected.store(true, _STD memory_order_release);
						this->failure_logged = false;
						LOG_INFO("WebSocket 已连接: {}", WsClient::buildWsUrl(this->host, kServerPort));
						plane::domain::PlaneStateStore::getInstance().update(
							[](plane::domain::PlaneStateDataClass& st)
							{
								st.web_socket_connected = true;
							}
						);
						this->doSubscribe();
					}
				);
			}
		);
	}

	void WsClient::Impl::doSubscribe()
	{
		this->subscribe_payload = WsClient::buildSubscribePayload(subscribeTypes());
		this->ws->async_write(
			asio::buffer(this->subscribe_payload),
			[this](const WsError& ec, _STD size_t)
			{
				if (ec)
				{
					this->onIoError("订阅发送", ec);
					return;
				}
				if (!this->conn_active.load(_STD memory_order_acquire))
				{
					return;
				}

				LOG_INFO("WebSocket 已发送订阅: {}", this->subscribe_payload);
				this->startRead();
				this->schedulePing();
			}
		);
	}

	void WsClient::Impl::startRead()
	{
		this->ws->async_read(
			this->rx_buffer,
			[this](const WsError& ec, _STD size_t)
			{
				if (ec)
				{
					this->onIoError("数据接收", ec);
					return;
				}
				if (!this->conn_active.load(_STD memory_order_acquire))
				{
					return;
				}

				this->onMessage();
				if (this->running.load(_STD memory_order_acquire))
				{
					this->startRead(); // 持续接收
				}
			}
		);
	}

	void WsClient::Impl::onMessage()
	{
		const _STD string text { beast::buffers_to_string(this->rx_buffer.data()) };
		this->rx_buffer.consume(this->rx_buffer.size());

		// 仅提取 attributeType 作为日志标识; 业务处理 (swarmState/event/taskStatus) 后续接入
		_STD string type { "-" };
		try
		{
			const _NLOHMANN_JSON json message = _NLOHMANN_JSON json::parse(text);
			if (message.is_object() && message.contains("attributeType") && message["attributeType"].is_string())
			{
				type = message["attributeType"].get<_STD string>();
			}
		}
		catch (const _STD exception&)
		{
			// 非 JSON 报文: 按原文记录
		}

		const _STD size_t	   limit { text.size() < kPayloadLogLimit ? text.size() : kPayloadLogLimit };
		const _STD string_view preview { text.data(), limit };
		LOG_DEBUG("WebSocket 数据 [{}] ({} 字节): {}{}", type, text.size(), preview, text.size() > kPayloadLogLimit ? " ...(已截断)" : "");
	}

	// ============================ 保活 ============================

	void WsClient::Impl::schedulePing()
	{
		this->ping_timer.expires_after(kPingInterval);
		this->ping_timer.async_wait(
			[this](const WsError& ec)
			{
				if (ec || !this->conn_active.load(_STD memory_order_acquire))
				{
					return; // 已取消 (断开/停止)
				}
				this->doPing();
			}
		);
	}

	void WsClient::Impl::doPing()
	{
		if (!this->ws || !this->conn_active.load(_STD memory_order_acquire))
		{
			return;
		}

		// 先布置 pong 等待超时: 服务端不响应 ping 时判定死链 (对齐 OkHttp ping 无响应 -> 连接失效)
		this->ping_timeout_timer.expires_after(kPingTimeout);
		this->ping_timeout_timer.async_wait(
			[this](const WsError& ec)
			{
				if (ec || !this->conn_active.load(_STD memory_order_acquire))
				{
					return; // pong 已在时限内返回 (计时已取消)
				}
				LOG_WARN("WebSocket ping 未在 {}s 内收到 pong, 判定连接失效", kPingTimeout.count());
				this->teardown("ping 超时");
			}
		);

		this->ws->async_ping(
			beast::websocket::ping_data {},
			[this](const WsError& ec)
			{
				this->ping_timeout_timer.cancel(); // pong 已返回 (或出错)
				if (ec)
				{
					this->onIoError("ping", ec);
					return;
				}
				if (this->conn_active.load(_STD memory_order_acquire))
				{
					this->schedulePing(); // 下一轮
				}
			}
		);
	}

	// ============================ 错误处理与重连 ============================

	void WsClient::Impl::onIoError(const char* stage, const WsError& ec)
	{
		if (!this->conn_active.load(_STD memory_order_acquire))
		{
			return; // 清理流程中完成的操作, 忽略
		}
		this->teardown(_STD string(stage) + ": " + ec.message());
	}

	void WsClient::Impl::closeTransport()
	{
		this->ping_timer.cancel();
		this->ping_timeout_timer.cancel();
		this->retry_timer.cancel();

		if (this->ws)
		{
			beast::error_code ignored {};
			auto&			  socket { beast::get_lowest_layer(*this->ws).socket() };
			socket.shutdown(asio::ip::tcp::socket::shutdown_both, ignored);
			socket.close(ignored);
		}
	}

	void WsClient::Impl::teardown(const _STD string& reason)
	{
		// 幂等: 多个回调可能同时报告错误, 仅首个生效
		if (!this->conn_active.exchange(false, _STD memory_order_acq_rel))
		{
			return;
		}

		this->connected.store(false, _STD memory_order_release);
		plane::domain::PlaneStateStore::getInstance().update(
			[](plane::domain::PlaneStateDataClass& st)
			{
				st.web_socket_connected = false;
			}
		);

		if (!this->running.load(_STD memory_order_acquire))
		{
			return; // 手动停止: 不再重连
		}

		if (!this->failure_logged)
		{
			LOG_WARN("WebSocket 连接中断 ({}), {}s 后自动重连", reason, kReconnectInterval.count());
			this->failure_logged = true;
		}
		this->scheduleReconnect();
	}

	void WsClient::Impl::scheduleReconnect()
	{
		this->retry_timer.expires_after(kReconnectInterval);
		this->retry_timer.async_wait(
			[this](const WsError& ec)
			{
				if (ec)
				{
					return;
				}
				this->scheduleConnect();
			}
		);
	}

	// ============================ 对外接口 ============================

	WsClient::WsClient(void) noexcept: impl_(_STD make_unique<Impl>()) {}

	WsClient::~WsClient(void) noexcept
	{
		// 兜底停止; 此处不记日志, 避免静态析构期访问日志系统
		if (this->started_.exchange(false, _STD memory_order_acq_rel))
		{
			this->impl_->stop();
		}
	}

	WsClient& WsClient::getInstance(void) noexcept
	{
		static WsClient instance {};
		return instance;
	}

	void WsClient::start(void) noexcept
	{
		if (this->started_.exchange(true, _STD memory_order_acq_rel))
		{
			return; // 已启动 (幂等)
		}

		LOG_INFO("WebSocket 客户端启动 (等待目录就绪)");
		this->impl_->start();
	}

	void WsClient::stop(void) noexcept
	{
		if (!this->started_.exchange(false, _STD memory_order_acq_rel))
		{
			return; // 未启动 (幂等)
		}

		this->impl_->stop();
		LOG_INFO("WebSocket 客户端已停止");
	}

	bool WsClient::isConnected(void) const noexcept
	{
		return this->impl_->connected.load(_STD memory_order_acquire);
	}
} // namespace plane::manager

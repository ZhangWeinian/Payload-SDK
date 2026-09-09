// cy_psdk/manager/catalog/client/internal/UdpDiscoveryClient.cpp

#include "manager/catalog/client/internal/UdpDiscoveryClient.h"

#include <fmt/format.h>
#include <asio.hpp>
#include <chrono>
#include <map>
#include <random>

#include "manager/catalog/client/internal/ProbePacketCodec.h"
#include "manager/catalog/client/internal/TargetExpander.h"

#include "define.h"

namespace plane::catalog::internal
{
	namespace
	{
		constexpr int kMaxScanTargets = 1024;

		// 生成随机 request id (对齐 java ThreadLocalRandom.nextInt())
		_NODISCARD int randomRequestId(void)
		{
			static _STD mt19937 generator { static_cast<unsigned>(_STD_CHRONO steady_clock::now().time_since_epoch().count()) };
			return static_cast<int>(generator());
		}
	} // namespace

	UdpDiscoveryClient::UdpDiscoveryClient(_STD optional<_STD string> cache_file, _STD string bind_address):
		cache_(cache_file.has_value() ? _STD make_unique<CatalogIpCache>(_STD move(*cache_file)) : nullptr),
		bind_address_(_STD move(bind_address))
	{}

	void UdpDiscoveryClient::saveSuccessfulIp(const _STD string& ip)
	{
		if (this->cache_)
		{
			this->cache_->save(ip);
		}
	}

	DiscoveryReport UdpDiscoveryClient::invalid(const _STD string& message)
	{
		DiscoveryReport report {};
		report.status = DiscoveryStatus::INVALID_ARGUMENT;
		report.error  = message;
		return report;
	}

	DiscoveryReport UdpDiscoveryClient::notFound(const _STD string& message)
	{
		DiscoveryReport report {};
		report.status = DiscoveryStatus::NOT_FOUND;
		report.error  = message;
		return report;
	}

	DiscoveryReport UdpDiscoveryClient::socketError(const _STD string& message)
	{
		DiscoveryReport report {};
		report.status = DiscoveryStatus::SOCKET_ERROR;
		report.error  = message;
		return report;
	}

	DiscoveryReport UdpDiscoveryClient::discover(const DiscoveryConfig& config, const _STD atomic<bool>& cancelled)
	{
		if (config.node_id.empty())
		{
			return this->invalid("nodeId 为空");
		}
		if (config.port <= 0 || config.port > 65'535)
		{
			return this->invalid(_FMT format("非法端口: {}", config.port));
		}
		if (config.response_window.count() <= 0)
		{
			return this->invalid("非法超时");
		}

		Result<_STD vector<_STD string>> expanded { expandTargets(config.targets, kMaxScanTargets) };
		if (!expanded.isOk())
		{
			return this->invalid(expanded.error().message);
		}
		_STD vector<_STD string> targets { expanded.value() };
		if (this->cache_)
		{
			targets = this->cache_->prioritize(targets);
		}

		try
		{
			_ASIO io_context io {};
			_ASIO ip::udp::socket socket { io };
			socket.open(_ASIO ip::udp::v4());
			socket.set_option(_ASIO socket_base::broadcast(true));
			if (!this->bind_address_.empty())
			{
				_ASIO error_code bind_ec {};
				socket.bind(_ASIO ip::udp::endpoint { _ASIO ip::make_address(this->bind_address_), 0 }, bind_ec);
				if (bind_ec)
				{
					// 绑定地址失效 (如本机 IP 变化) -> 回退通配绑定
					socket.bind(_ASIO ip::udp::endpoint { _ASIO ip::udp::v4(), 0 });
				}
			}
			else
			{
				socket.bind(_ASIO ip::udp::endpoint { _ASIO ip::udp::v4(), 0 });
			}
			socket.non_blocking(true);

			// 编码探测请求
			ProbePacket probe {};
			probe.command	 = PROBE_COMMAND;
			probe.node_id	 = config.node_id;
			probe.request_id = randomRequestId();
			Result<_STD vector<_STD uint8_t>> encoded { encodeProbePacket(probe) };
			if (!encoded.isOk())
			{
				return this->invalid(encoded.error().message);
			}
			const _STD vector<_STD uint8_t> request { encoded.value() };

			// 逐目标发送
			bool sent { false };
			for (const auto& target : targets)
			{
				if (cancelled.load(_STD memory_order_acquire))
				{
					return this->notFound("探测已取消");
				}
				_ASIO error_code ec {};
				const _ASIO ip::udp::endpoint peer { _ASIO ip::make_address(target), static_cast<unsigned short>(config.port) };
				socket.send_to(_ASIO buffer(request), peer, 0, ec);
				if (ec == _ASIO error::would_block)
				{
					continue; // 发送缓冲区满 (极小概率), 跳过该目标
				}
				if (ec)
				{
					return this->socketError(ec.message());
				}
				sent = true;
			}
			if (!sent)
			{
				return this->socketError("发送失败");
			}

			// 在响应窗口内收集响应
			using Clock = _STD_CHRONO steady_clock;
			const Clock::time_point	  deadline { Clock::now() + config.response_window };

			// 去重保序 (key = instanceId 或源 IP)
			_STD vector<_STD string> order {};
			_STD map<_STD string, CatalogEndpoint> by_key {};
			_STD map<_STD string, bool> success_by_key {};

			auto						handlePacket = [&](const _STD vector<_STD uint8_t>& data, const _ASIO ip::udp::endpoint& remote)
			{
				Result<ProbePacket> decoded { decodeProbePacket(data) };
				if (!decoded.isOk())
				{
					return;
				}
				const ProbePacket& packet { decoded.value() };
				if (packet.command != PROBE_RESPONSE_COMMAND)
				{
					return;
				}
				if (packet.node_id != config.node_id)
				{
					return;
				}
				if (packet.has_request_id && packet.request_id != probe.request_id)
				{
					return;
				}
				if (packet.http_port == 0)
				{
					return;
				}

				_STD string ip { packet.ip };
				if (ip.empty())
				{
					ip = remote.address().to_string();
				}
				const _STD string key { packet.instance_id.empty() ? ip : packet.instance_id };

				CatalogEndpoint	  endpoint {};
				endpoint.instance_id = packet.instance_id;
				endpoint.ip			 = ip;
				endpoint.http_port	 = packet.http_port;
				endpoint.node_name	 = packet.node_name;
				if (by_key.find(key) == by_key.end())
				{
					by_key[key] = endpoint;
					order.push_back(key);
				}
				success_by_key[key] = packet.status == 1;
			};

			while (true)
			{
				if (cancelled.load(_STD memory_order_acquire))
				{
					break;
				}
				const auto remain { _STD_CHRONO duration_cast<_STD_CHRONO milliseconds>(deadline - Clock::now()) };
				if (remain.count() <= 0)
				{
					break;
				}

				io.restart();
				_STD vector<_STD uint8_t> buffer(1024);
				_ASIO ip::udp::endpoint remote {};
				bool					got { false };
				socket.async_receive_from(
					_ASIO buffer(buffer),
					remote,
					[&](const _ASIO error_code& ec, _STD size_t length)
					{
						if (ec)
						{
							return;
						}
						got = true;
						buffer.resize(length);
						handlePacket(buffer, remote);
					}
				);
				io.run_one_for(remain);
				if (!got)
				{
					break; // 窗口内无更多数据
				}
			}

			DiscoveryReport report {};
			for (const auto& key : order)
			{
				if (success_by_key[key])
				{
					report.endpoints.push_back(by_key[key]);
				}
			}
			report.multiple_instances = by_key.size() > 1;
			if (report.endpoints.empty())
			{
				report.status = DiscoveryStatus::NOT_FOUND;
				report.error  = "catalog not found";
			}
			else
			{
				report.status = DiscoveryStatus::OK;
			}
			return report;
		}
		catch (const _STD exception& ex)
		{
			return this->socketError(ex.what());
		}
	}
} // namespace plane::catalog::internal

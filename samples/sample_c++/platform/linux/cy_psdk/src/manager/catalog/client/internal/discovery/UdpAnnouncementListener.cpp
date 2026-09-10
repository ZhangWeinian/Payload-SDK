// cy_psdk/manager/catalog/client/internal/discovery/UdpAnnouncementListener.cpp

#include "manager/catalog/client/internal/discovery/UdpAnnouncementListener.h"

#include <fmt/format.h>
#include <asio.hpp>
#include <chrono>
#include <cstdint>
#include <poll.h>
#include <thread>
#include <vector>

#include "manager/catalog/client/CatalogError.h"
#include "manager/catalog/client/CatalogFailure.h"
#include "manager/catalog/client/internal/codec/ProbePacketCodec.h"

#include "define.h"

namespace plane::catalog::internal
{
	namespace
	{
		// 接收超时: 定期醒来检查停止标志
		constexpr int kReceiveTimeoutMs = 500;
	} // namespace

	struct UdpAnnouncementListener::Impl
	{
		int				 port { 0 };
		_STD string		 bind_address {};
		Callback		 callback {};
		_ASIO io_context io {};
		_ASIO ip::udp::socket socket { io };
		_STD atomic<bool> running { false };
		_STD thread		  thread {};

		Impl(int port_value, _STD string address, Callback cb): port(port_value), bind_address(_STD move(address)), callback(_STD move(cb)) {}

		void runLoop(void)
		{
			_STD vector<_STD uint8_t> buffer(1024);
			while (this->running.load(_STD memory_order_acquire))
			{
				// asio 同步接收为内部无限等待 (忽略 SO_RCVTIMEO), 这里用 poll 限时等待,
				// 保证 stop() 时最多等待一个接收周期即可安全 join
				_CSTD pollfd descriptor { this->socket.native_handle(), POLLIN, 0 };
				const int	 ready { _CSTD poll(&descriptor, 1, kReceiveTimeoutMs) };
				if (ready < 0)
				{
					_STD this_thread::sleep_for(_STD_CHRONO milliseconds { 50 }); // 瞬时错误: 避免忙循环
					continue;
				}
				if (ready == 0)
				{
					continue; // 超时: 周期醒来检查停止标志
				}
				if ((descriptor.revents & (POLLERR | POLLNVAL)) != 0)
				{
					_STD this_thread::sleep_for(_STD_CHRONO milliseconds { 50 });
					continue;
				}

				_ASIO ip::udp::endpoint remote {};
				_ASIO error_code		ec {};
				const _STD size_t		length { this->socket.receive_from(_ASIO buffer(buffer), remote, 0, ec) };
				if (ec)
				{
					if (!this->running.load(_STD memory_order_acquire))
					{
						break;
					}
					if (ec == _ASIO error::try_again || ec == _ASIO error::would_block)
					{
						continue;												  // 非阻塞接收无数据 (被抢占): 继续等待
					}
					_STD this_thread::sleep_for(_STD_CHRONO milliseconds { 50 }); // 瞬时错误: 避免忙循环
					continue;
				}

				const _STD vector<_STD uint8_t> packet { buffer.begin(), buffer.begin() + static_cast<_STD ptrdiff_t>(length) };
				Result<CatalogAnnouncement>		decoded { decodeAnnouncement(packet) };
				if (!decoded.isOk())
				{
					continue;
				}
				CatalogAnnouncement announcement { decoded.value() };
				if (announcement.ip.empty() && remote.address().is_v4())
				{
					// 公告未携带 ip 时回退为 UDP 源地址
					announcement.ip = remote.address().to_string();
				}
				if (announcement.node_id.empty())
				{
					continue;
				}
				try
				{
					this->callback(announcement);
				}
				catch (...)
				{
					// 回调异常不得终止监听循环
				}
			}
		}
	};

	UdpAnnouncementListener::UdpAnnouncementListener(int port, _STD string bind_address, Callback callback):
		impl_(_STD make_unique<Impl>(port, _STD move(bind_address), _STD move(callback)))
	{}

	UdpAnnouncementListener::UdpAnnouncementListener(int port, Callback callback): UdpAnnouncementListener(port, "", _STD move(callback)) {}

	UdpAnnouncementListener::~UdpAnnouncementListener(void)
	{
		this->stop();
	}

	_NODISCARD Result<void> UdpAnnouncementListener::start(void)
	{
		auto& impl { *this->impl_ };
		if (impl.running.load(_STD memory_order_acquire))
		{
			return Result<void>::success(); // 幂等
		}
		if (impl.port <= 0 || impl.port > 65'535)
		{
			return Result<void>::failure(makeFailure(CatalogError::INVALID_ARGUMENT, _FMT format("非法端口: {}", impl.port)));
		}

		_ASIO error_code ignored {};
		impl.socket.close(ignored); // 幂等: 允许 stop 后再次 start
		_ASIO error_code ec {};
		impl.socket.open(_ASIO ip::udp::v4(), ec);
		if (ec)
		{
			return Result<void>::failure(makeFailure(CatalogError::CATALOG_UNAVAILABLE, _FMT format("open failed: {}", ec.message())));
		}

		_ASIO ip::udp::endpoint local {};
		if (impl.bind_address.empty())
		{
			local = _ASIO ip::udp::endpoint { _ASIO ip::udp::v4(), static_cast<unsigned short>(impl.port) };
		}
		else
		{
			_ASIO error_code addr_ec {};
			local = _ASIO	 ip::udp::endpoint { _ASIO ip::make_address(impl.bind_address, addr_ec), static_cast<unsigned short>(impl.port) };
			if (addr_ec)
			{
				impl.socket.close(ignored);
				return Result<void>::failure(makeFailure(CatalogError::INVALID_ARGUMENT, _FMT format("非法绑定地址: {}", impl.bind_address)));
			}
		}

		impl.socket.set_option(_ASIO socket_base::reuse_address(true), ignored);
		impl.socket.bind(local, ec);
		if (ec)
		{
			impl.socket.close(ignored);
			return Result<void>::failure(makeFailure(CatalogError::CATALOG_UNAVAILABLE, _FMT format("bind failed: {}", ec.message())));
		}

		// 非阻塞接收 + poll 限时等待 (asio 同步接收在阻塞模式下无法限时)
		impl.socket.non_blocking(true, ignored);

		impl.running.store(true, _STD memory_order_release);
		impl.thread = _STD thread(
			[this]()
			{
				this->impl_->runLoop();
			}
		);
		return Result<void>::success();
	}

	void UdpAnnouncementListener::stop(void) noexcept
	{
		auto& impl { *this->impl_ };
		impl.running.store(false, _STD memory_order_release);
		if (impl.thread.joinable())
		{
			impl.thread.join(); // 最多等待一个接收超时周期 (500ms)
		}
		_ASIO error_code ignored {};
		impl.socket.close(ignored);
	}

	_NODISCARD bool UdpAnnouncementListener::running(void) const noexcept
	{
		return this->impl_->running.load(_STD memory_order_acquire);
	}
} // namespace plane::catalog::internal

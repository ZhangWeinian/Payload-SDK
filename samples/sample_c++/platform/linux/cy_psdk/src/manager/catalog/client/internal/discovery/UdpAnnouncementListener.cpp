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
        int                     port { 0 };
        ::std::string           bind_address {};
        Callback                callback {};
        ::asio::io_context      io {};
        ::asio::ip::udp::socket socket { io };
        ::std::atomic<bool>     running { false };
        ::std::thread           thread {};

        Impl(int port_value, ::std::string address, Callback cb): port(port_value), bind_address(::std::move(address)), callback(::std::move(cb))
        {}

        void runLoop(void)
        {
            ::std::vector<::std::uint8_t> buffer(1024);
            while (this->running.load(::std::memory_order_acquire))
            {
                // asio 同步接收为内部无限等待 (忽略 SO_RCVTIMEO), 这里用 poll 限时等待,
                // 保证 stop() 时最多等待一个接收周期即可安全 join
                ::pollfd  descriptor { this->socket.native_handle(), POLLIN, 0 };
                const int ready { ::poll(&descriptor, 1, kReceiveTimeoutMs) };
                if (ready < 0)
                {
                    ::std::this_thread::sleep_for(::std::chrono::milliseconds { 50 }); // 瞬时错误: 避免忙循环
                    continue;
                }
                if (ready == 0)
                {
                    continue; // 超时: 周期醒来检查停止标志
                }
                if ((static_cast<unsigned>(descriptor.revents) & static_cast<unsigned>(POLLERR | POLLNVAL)) != 0)
                {
                    ::std::this_thread::sleep_for(::std::chrono::milliseconds { 50 });
                    continue;
                }

                ::asio::ip::udp::endpoint remote {};
                ::asio::error_code        ec {};
                const ::std::size_t       length { this->socket.receive_from(::asio::buffer(buffer), remote, 0, ec) };
                if (ec)
                {
                    if (!this->running.load(::std::memory_order_acquire))
                    {
                        break;
                    }
                    if (ec == ::asio::error::try_again || ec == ::asio::error::would_block)
                    {
                        continue;                                                      // 非阻塞接收无数据 (被抢占): 继续等待
                    }
                    ::std::this_thread::sleep_for(::std::chrono::milliseconds { 50 }); // 瞬时错误: 避免忙循环
                    continue;
                }

                const ::std::vector<::std::uint8_t> packet { buffer.begin(), buffer.begin() + static_cast<::std::ptrdiff_t>(length) };
                Result<CatalogAnnouncement>         decoded { decodeAnnouncement(packet) };
                if (!decoded.has_value())
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
                catch (...) // NOLINT(bugprone-empty-catch)
                {
                    // 回调异常不得终止监听循环
                }
            }
        }
    };

    UdpAnnouncementListener::UdpAnnouncementListener(
        int           port,
        ::std::string bind_address,
        Callback      callback
    ): impl_(::std::make_unique<Impl>(port, ::std::move(bind_address), ::std::move(callback)))
    {}

    UdpAnnouncementListener::UdpAnnouncementListener(int port, Callback callback): UdpAnnouncementListener(port, "", ::std::move(callback)) {}

    UdpAnnouncementListener::~UdpAnnouncementListener(void)
    {
        this->stop();
    }

    [[nodiscard]] Result<void> UdpAnnouncementListener::start(void)
    {
        auto& impl { *this->impl_ };
        if (impl.running.load(::std::memory_order_acquire))
        {
            return {}; // 幂等
        }
        if (impl.port <= 0 || impl.port > 65'535)
        {
            return ::std::unexpected(makeFailure(CatalogError::INVALID_ARGUMENT, ::fmt::format("非法端口: {}", impl.port)));
        }

        ::asio::error_code ignored {};
        impl.socket.close(ignored); // 幂等: 允许 stop 后再次 start
        ::asio::error_code ec {};
        impl.socket.open(::asio::ip::udp::v4(), ec);
        if (ec)
        {
            return ::std::unexpected(makeFailure(CatalogError::CATALOG_UNAVAILABLE, ::fmt::format("open failed: {}", ec.message())));
        }

        ::asio::ip::udp::endpoint local {};
        if (impl.bind_address.empty())
        {
            local = ::asio::ip::udp::endpoint { ::asio::ip::udp::v4(), static_cast<unsigned short>(impl.port) };
        }
        else
        {
            ::asio::error_code addr_ec {};
            local = ::asio::ip::udp::endpoint { ::asio::ip::make_address(impl.bind_address, addr_ec), static_cast<unsigned short>(impl.port) };
            if (addr_ec)
            {
                impl.socket.close(ignored);
                return ::std::unexpected(makeFailure(CatalogError::INVALID_ARGUMENT, ::fmt::format("非法绑定地址: {}", impl.bind_address)));
            }
        }

        impl.socket.set_option(::asio::socket_base::reuse_address(true), ignored);
        impl.socket.bind(local, ec);
        if (ec)
        {
            impl.socket.close(ignored);
            return ::std::unexpected(makeFailure(CatalogError::CATALOG_UNAVAILABLE, ::fmt::format("bind failed: {}", ec.message())));
        }

        // 非阻塞接收 + poll 限时等待 (asio 同步接收在阻塞模式下无法限时)
        impl.socket.non_blocking(true, ignored);

        impl.running.store(true, ::std::memory_order_release);
        impl.thread = ::std::thread(
            [this]()
            {
                this->impl_->runLoop();
            }
        );
        return {};
    }

    void UdpAnnouncementListener::stop(void) noexcept
    {
        auto& impl { *this->impl_ };
        impl.running.store(false, ::std::memory_order_release);
        if (impl.thread.joinable())
        {
            impl.thread.join(); // 最多等待一个接收超时周期 (500ms)
        }
        ::asio::error_code ignored {};
        impl.socket.close(ignored);
    }

    [[nodiscard]] bool UdpAnnouncementListener::running(void) const noexcept
    {
        return this->impl_->running.load(::std::memory_order_acquire);
    }
} // namespace plane::catalog::internal

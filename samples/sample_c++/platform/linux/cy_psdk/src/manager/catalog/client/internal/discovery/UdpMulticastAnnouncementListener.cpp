// cy_psdk/manager/catalog/client/internal/discovery/UdpMulticastAnnouncementListener.cpp

#include "manager/catalog/client/internal/discovery/UdpMulticastAnnouncementListener.h"

#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <asio.hpp>
#include <chrono>
#include <poll.h>
#include <thread>
#include <vector>

#include "manager/catalog/client/CatalogError.h"
#include "manager/catalog/client/CatalogFailure.h"

#include "define.h"

namespace plane::catalog::internal
{
    namespace
    {
        // 接收超时: 定期醒来检查停止标志
        constexpr int kReceiveTimeoutMs = 500;

        // 单包接收缓冲: 与服务端 16384 字节发送缓冲对齐
        constexpr ::std::size_t kBufferSize = 16'384;

        // 宽松取值: 组播报文来自外部, 个别字段类型异常不应导致整条公告被丢弃
        [[nodiscard]] long long optionalInt64(const ::nlohmann::json& json, const char* field)
        {
            if (!json.contains(field) || !json[field].is_number())
            {
                return 0;
            }
            return json[field].get<long long>();
        }

        [[nodiscard]] ::std::string optionalString(const ::nlohmann::json& json, const char* field)
        {
            if (!json.contains(field) || !json[field].is_string())
            {
                return {};
            }
            return json[field].get<::std::string>();
        }

        // 拆分 accessAddress 为 host/port; 容忍缺失 scheme、带路径、[IPv6] 等写法。
        // 端口解析失败保持 0 (超过 65535 视为非法, 同样回落 0)。
        void splitAccessAddress(const ::std::string& address_value, ::std::string& host_out, int& port_out)
        {
            host_out.clear();
            port_out = 0;

            ::std::string       address { address_value };
            const ::std::size_t scheme { address.find("://") };
            if (scheme != ::std::string::npos)
            {
                address.erase(0, scheme + 3);
            }
            const ::std::size_t slash { address.find('/') };
            if (slash != ::std::string::npos)
            {
                address.erase(slash);
            }
            if (address.empty())
            {
                return;
            }
            const ::std::size_t colon { address.rfind(':') };
            if (colon == ::std::string::npos)
            {
                host_out = address;
                return;
            }
            host_out = address.substr(0, colon);
            if (host_out.size() >= 2 && host_out.front() == '[' && host_out.back() == ']')
            {
                host_out = host_out.substr(1, host_out.size() - 2);
            }

            const ::std::string port_text { address.substr(colon + 1) };
            int                 value { 0 };
            bool                valid { !port_text.empty() };
            for (const char ch : port_text)
            {
                if (ch < '0' || ch > '9')
                {
                    valid = false;
                    break;
                }
                value = (value * 10) + (ch - '0');
                if (value > 65'535)
                {
                    valid = false;
                    break;
                }
            }
            port_out = valid ? value : 0;
        }
    } // namespace

    struct UdpMulticastAnnouncementListener::Impl
    {
        ::std::string           group {};
        int                     port { 0 };
        ::std::string           bind_address {};
        Callback                callback {};
        ::asio::io_context      io {};
        ::asio::ip::udp::socket socket { io };
        ::std::atomic<bool>     running { false };
        ::std::thread           thread {};

        Impl(::std::string group_value, int port_value, ::std::string address, Callback cb): group(::std::move(group_value)),
                                                                                             port(port_value),
                                                                                             bind_address(::std::move(address)),
                                                                                             callback(::std::move(cb))
        {}

        void runLoop(void)
        {
            ::std::vector<::std::uint8_t> buffer(kBufferSize);
            while (this->running.load(::std::memory_order_acquire))
            {
                // 与 UdpAnnouncementListener 同策略: asio 同步接收为内部无限等待,
                // 这里用 poll 限时等待, 保证 stop() 时最多等一个接收周期即可安全 join
                ::pollfd  descriptor { this->socket.native_handle(), POLLIN, 0 };
                const int ready { ::poll(&descriptor, 1, kReceiveTimeoutMs) };
                if (ready < 0)
                {
                    ::std::this_thread::sleep_for(::std::chrono::milliseconds { 50 });
                    continue;
                }
                if (ready == 0)
                {
                    continue;
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
                        continue;
                    }
                    ::std::this_thread::sleep_for(::std::chrono::milliseconds { 50 });
                    continue;
                }

                const ::std::string               payload { buffer.begin(), buffer.begin() + static_cast<::std::ptrdiff_t>(length) };
                Result<MulticastNodeAnnouncement> decoded { UdpMulticastAnnouncementListener::parse(payload) };
                if (!decoded.has_value())
                {
                    continue; // 非本协议报文: 静默忽略
                }
                const ::std::string source_ip { remote.address().is_v4() ? remote.address().to_string() : ::std::string {} };
                try
                {
                    this->callback(decoded.value(), source_ip);
                }
                catch (...) // NOLINT(bugprone-empty-catch)
                {
                    // 回调异常不得终止监听循环
                }
            }
        }
    };

    UdpMulticastAnnouncementListener::UdpMulticastAnnouncementListener(
        Callback callback
    ): UdpMulticastAnnouncementListener(::std::string { DEFAULT_GROUP }, DEFAULT_PORT, "", ::std::move(callback))
    {}

    UdpMulticastAnnouncementListener::UdpMulticastAnnouncementListener(
        ::std::string group,
        int           port,
        ::std::string bind_address,
        Callback      callback
    ): impl_(::std::make_unique<Impl>(::std::move(group), port, ::std::move(bind_address), ::std::move(callback)))
    {}

    UdpMulticastAnnouncementListener::~UdpMulticastAnnouncementListener(void)
    {
        this->stop();
    }

    [[nodiscard]] Result<MulticastNodeAnnouncement> UdpMulticastAnnouncementListener::parse(const ::std::string& payload)
    {
        ::nlohmann::json root {};
        try
        {
            root = ::nlohmann::json::parse(payload);
        }
        catch (const ::std::exception& ex)
        {
            return ::std::unexpected(makeFailure(CatalogError::PROTOCOL_ERROR, ::std::string { "invalid json: " } + ex.what()));
        }
        if (!root.is_object())
        {
            return ::std::unexpected(makeFailure(CatalogError::PROTOCOL_ERROR, "announcement must be an object"));
        }
        if (!root.contains("type") || !root["type"].is_string() || root["type"].get<::std::string>() != MulticastNodeAnnouncement::TYPE)
        {
            return ::std::unexpected(makeFailure(CatalogError::PROTOCOL_ERROR, "unexpected announcement type"));
        }
        if (!root.contains("node") || !root["node"].is_object())
        {
            return ::std::unexpected(makeFailure(CatalogError::PROTOCOL_ERROR, "missing or invalid field: node"));
        }
        const ::nlohmann::json& node { root["node"] };
        if (!node.contains("accessAddress") || !node["accessAddress"].is_string())
        {
            return ::std::unexpected(makeFailure(CatalogError::PROTOCOL_ERROR, "missing or invalid field: accessAddress"));
        }

        MulticastNodeAnnouncement announcement {};
        announcement.type                 = root["type"].get<::std::string>();
        announcement.timestamp            = optionalInt64(root, "timestamp");
        announcement.local_name           = optionalString(node, "localName");
        announcement.deployment_location  = optionalString(node, "deploymentLocation");
        announcement.node_purpose         = optionalString(node, "nodePurpose");
        announcement.department           = optionalString(node, "department");
        announcement.access_address       = node["accessAddress"].get<::std::string>();
        announcement.online_service_count = static_cast<int>(optionalInt64(node, "onlineServiceCount"));
        splitAccessAddress(announcement.access_address, announcement.ip, announcement.http_port);
        return announcement;
    }

    [[nodiscard]] Result<void> UdpMulticastAnnouncementListener::start(void)
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
        if (impl.group.empty())
        {
            return ::std::unexpected(makeFailure(CatalogError::INVALID_ARGUMENT, "组播地址为空"));
        }

        ::asio::error_code        address_ec {};
        const ::asio::ip::address group_address { ::asio::ip::make_address(impl.group, address_ec) };
        if (address_ec || !group_address.is_v4())
        {
            return ::std::unexpected(makeFailure(CatalogError::INVALID_ARGUMENT, ::fmt::format("非法组播地址: {}", impl.group)));
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
            ::asio::error_code bind_ec {};
            local = ::asio::ip::udp::endpoint { ::asio::ip::make_address(impl.bind_address, bind_ec), static_cast<unsigned short>(impl.port) };
            if (bind_ec)
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

        // 加入组播组 (不指定接口 -> 由系统选择默认组播接口)
        ::asio::error_code join_ec {};
        impl.socket.set_option(::asio::ip::multicast::join_group(group_address.to_v4()), join_ec);
        if (join_ec)
        {
            impl.socket.close(ignored);
            return ::std::unexpected(makeFailure(CatalogError::CATALOG_UNAVAILABLE, ::fmt::format("join group failed: {}", join_ec.message())));
        }

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

    void UdpMulticastAnnouncementListener::stop(void) noexcept
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

    [[nodiscard]] bool UdpMulticastAnnouncementListener::running(void) const noexcept
    {
        return this->impl_->running.load(::std::memory_order_acquire);
    }
} // namespace plane::catalog::internal

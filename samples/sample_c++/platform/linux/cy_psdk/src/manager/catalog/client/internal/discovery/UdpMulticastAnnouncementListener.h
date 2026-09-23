// cy_psdk/manager/catalog/client/internal/discovery/UdpMulticastAnnouncementListener.h
//
// 被动监听 Catalog 服务端的组播节点公告 (UDP 组播 JSON, type=catalog-node-announce-v1,
// 默认每 ~5s 一次; 默认组 239.255.18.18:38500)。
//
// 与 UdpAnnouncementListener (旧通道: bind 30906 收 SWMP 二进制公告 command=0x82) 并存:
// 新服务端已改为在组播地址发布 JSON 公告, 旧通道保留用于兼容尚未升级的服务端。
//
// 生命周期与 UdpAnnouncementListener 相同: start() 后内部线程收包并串行回调,
// stop()/析构释放。非本协议报文 (type 不匹配或解析失败) 静默忽略。
//
// 公告只表达"节点存在及摘要", 不含服务明细; 需要服务列表/在线明细时再访问
// access_address 指向的 HTTP 接口。

#pragma once

#include <functional>
#include <memory>
#include <string>

#include "define.h"
#include "manager/catalog/client/CatalogModels.h"
#include "manager/catalog/client/Result.h"

namespace plane::catalog::internal
{
    class UdpMulticastAnnouncementListener
    {
    public:
        // 默认组播地址与端口 (服务端 configdata.json 的 multicastGroup / multicastPort)
        constexpr static const char* DEFAULT_GROUP = "239.255.18.18";
        constexpr static int         DEFAULT_PORT  = 38'500;

        // 公告回调; source_ip 为 UDP 源地址, 便于 access_address 缺失时兜底
        using Callback = ::std::function<void(const MulticastNodeAnnouncement&, const ::std::string&)>;

        // 使用默认组播地址与端口
        explicit UdpMulticastAnnouncementListener(Callback callback);

        // bind_address 为空则通配绑定 (多网卡场景可指定本机接口 IP)
        UdpMulticastAnnouncementListener(::std::string group, int port, ::std::string bind_address, Callback callback);
        ~UdpMulticastAnnouncementListener(void);

        UdpMulticastAnnouncementListener(const UdpMulticastAnnouncementListener&)            = delete;
        UdpMulticastAnnouncementListener& operator=(const UdpMulticastAnnouncementListener&) = delete;

        // 绑定端口、加入组播组并启动收包线程; 重复调用幂等
        [[nodiscard]] Result<void> start(void);

        // 停止监听并 join 收包线程 (最多等待一个接收超时周期)
        void               stop(void) noexcept;

        [[nodiscard]] bool running(void) const noexcept;

        // 解析组播公告 payload: type 必须为 MulticastNodeAnnouncement::TYPE,
        // node.accessAddress 必须存在。失败返回 PROTOCOL_ERROR, 调用方静默忽略该报文
        // (可能来自其它组播协议)。
        [[nodiscard]] static Result<MulticastNodeAnnouncement> parse(const ::std::string& payload);

    private:
        struct Impl;
        ::std::unique_ptr<Impl> impl_ {};
    };
} // namespace plane::catalog::internal

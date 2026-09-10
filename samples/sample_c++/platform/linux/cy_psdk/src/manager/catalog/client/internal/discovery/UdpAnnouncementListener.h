// cy_psdk/manager/catalog/client/internal/discovery/UdpAnnouncementListener.h
//
// 被动监听 Catalog 服务端主动广播的公告包 (SWMP command=0x82, 默认每 ~5s 一次)。
// 与 UdpDiscoveryClient (主动探测, 需先配置 node_id/targets) 不同,
// 本监听器在 CatalogRuntime 启动前即可独立工作: bind 端口收广播,
// 解析出 Catalog 节点信息 (node_id/ip/http_port), 用于构建"候选 Catalog 节点列表"。
//
// 生命周期由调用方控制: start() 后后台线程持续收包, stop()/析构释放。
// 回调在监听线程串行执行, 禁止在回调内做耗时操作。

#pragma once

#include <functional>
#include <memory>
#include <string>

#include "define.h"
#include "manager/catalog/client/CatalogModels.h"
#include "manager/catalog/client/Result.h"

namespace plane::catalog::internal
{
	class UdpAnnouncementListener
	{
	public:
		// 公告回调; 参数为已解析节点信息 (ip 为空时回退为 UDP 源地址)
		using Callback = _STD function<void(const CatalogAnnouncement&)>;

		// bind_address 为空则通配绑定
		UdpAnnouncementListener(int port, _STD string bind_address, Callback callback);
		UdpAnnouncementListener(int port, Callback callback);
		~UdpAnnouncementListener(void);

		UdpAnnouncementListener(const UdpAnnouncementListener&)			   = delete;
		UdpAnnouncementListener& operator=(const UdpAnnouncementListener&) = delete;

		// 启动监听 (bind 端口并启动收包线程); 可在 CatalogRuntime 未启动时调用
		_NODISCARD Result<void> start(void);

		// 停止监听并 join 收包线程 (最多等待一个接收超时周期)
		void			stop(void) noexcept;

		_NODISCARD bool running(void) const noexcept;

	private:
		struct Impl;
		_STD unique_ptr<Impl> impl_ {};
	};
} // namespace plane::catalog::internal

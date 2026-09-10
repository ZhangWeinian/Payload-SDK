// cy_psdk/manager/websocket/WsClient.h
//
// WebSocket 数据订阅客户端 (boost::beast), 对齐 msdk WebSocketRepository:
//   - 目录 READY 后连接 ws://<目录服务端IP>:8888 (对齐 msdk "ws://${registryIp}:8888")
//   - 连接成功立即订阅 stationSwarmState / event / stationTaskStatus (msdk 默认订阅集)
//   - 周期 ping 保活 (15s, 对齐 OkHttp pingInterval); pong 超时判定死链
//   - 断线/失败固定间隔 2s 自动重连 (对齐 serviceReconnectIntervalS 默认值), 无限重试;
//     手动 stop() 后不再重连
//   - 当前阶段仅接收并记录原始数据 (业务处理后续接入)
//
// 线程模型:
//   start() 派发后台线程运行 asio::io_context, 所有网络回调串行执行;
//   stop() 投递清理任务关闭连接/定时器后 join 后台线程; start/stop 幂等。

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "define.h"

namespace plane::manager
{
	class WsClient
	{
	public:
		static WsClient& getInstance(void) noexcept;

		// 后台启动: 等待目录就绪 -> 连接 -> 订阅 -> 接收数据; 重复调用无副作用
		void start(void) noexcept;

		// 停止: 关闭连接/定时器并 join 后台线程; 重复调用无副作用
		void stop(void) noexcept;

		// 当前是否已完成握手并保持连接 (日志/调试用)
		_NODISCARD bool isConnected(void) const noexcept;

		// 组装连接地址: "ws://<ip>:<port>"; ip 为空返回空串 (对齐 msdk webSocketUrl 派生规则)
		_NODISCARD static _STD string buildWsUrl(const _STD string& ip, std::uint16_t port)
		{
			if (ip.empty())
			{
				return {};
			}
			return "ws://" + ip + ":" + _STD to_string(port);
		}

		// 组装订阅报文: {"cmd":"subscribe","types":[...]} (对齐 msdk WebSocketRepository)
		_NODISCARD static _STD string buildSubscribePayload(const _STD vector<_STD string>& types)
		{
			_NLOHMANN_JSON json payload;
			payload["cmd"]	 = "subscribe"; // 固定命令字
			payload["types"] = types;		// 需要订阅的数据类型集合
			return payload.dump();
		}

	private:
		WsClient(void) noexcept;
		~WsClient(void) noexcept;
		WsClient(const WsClient&)			 = delete;
		WsClient& operator=(const WsClient&) = delete;

		struct Impl;
		_STD unique_ptr<Impl> impl_ {};

		// 后台线程生命周期 (防止 start()/stop() 重入)
		_STD atomic<bool> started_ { false };
	};
} // namespace plane::manager

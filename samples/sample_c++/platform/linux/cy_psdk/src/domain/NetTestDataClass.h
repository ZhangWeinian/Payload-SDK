// cy_psdk/domain/NetTestDataClass.h
#pragma once

#include <cstdint>
#include <string>

#include "define.h"

namespace plane::domain
{
	// 端口健康状态
	enum class PortHealth
	{
		UNKNOWN = 0, // /proc 不可读, 无法观测
		DEAD,		 // 端口无监听 → 服务未启动或已死
		LISTENING,	 // 有监听但无连接 → 等待客户端
		CONNECTED,	 // 有连接且发送队列在流动 → 正常
		STALLED,	 // 有连接但发送队列持续堆积 → 数据发不出去
		SILENT		 // 有连接但发送队列长期为零 → 发送端停止写入
	};

	struct PortHealthState
	{
		int			port { 0 };						// 监控端口 (TCP 帧 / RTSP)
		_STD string label { "" };					// 端口用途标签 (如 "TCP帧" / "RTSP")
		int			established_count { 0 };		// ESTABLISHED 连接数
		int64_t		tx_queue { 0 };					// 当前发送队列积压字节 (tx_queue)
		int64_t		updated_at { 0 };				// 最近观测时间戳
		PortHealth	health { PortHealth::UNKNOWN }; // 端口状态
	};

	// ping 命令单次结果 (仅原始输出字段; icmp_seq/ttl/time 为解析产物, 归业务层)
	struct PingResult
	{
		_STD string host { "" };		// 目标主机
		_STD string line_output { "" }; // ping 原始输出行
		_STD string info { "" };		// 附加信息
	};
} // namespace plane::domain

// cy_psdk/manager/catalog/client/internal/codec/ProbePacketCodec.h
//
// SWMP UDP 探测包编解码 (对齐 java ProbePacketCodec, 与服务端字节兼容)。
//
// 包布局 (大端):
//   magic(4B)=0x53574d50 "SWMP" | version(1B)=1 | command(1B) |
//   ip(1B len + bytes) | nodeId(1B len + bytes) | status(1B) |
//   nodeName(1B len + bytes) | requestId(4B) | instanceId(1B len + bytes) | httpPort(2B)

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "manager/catalog/client/CatalogModels.h"
#include "manager/catalog/client/Result.h"

#include "define.h"

namespace plane::catalog::internal
{
	// 探测包命令字
	enum ProbeCommand : _STD uint8_t {
		PROBE_COMMAND		   = 0X01, // 客户端探测请求
		PROBE_RESPONSE_COMMAND = 0X81, // 服务端响应
		PROBE_ANNOUNCE_COMMAND = 0X82  // 服务端主动广播公告
	};

	// 解码后的探测包内容 (对齐 java ProbePacket)
	struct ProbePacket
	{
		int			command { 0 };
		_STD string ip {};
		_STD string node_id {};
		int			status { 0 };
		_STD string node_name {};
		int			request_id { 0 };
		bool		has_request_id { false };
		_STD string instance_id {};
		int			http_port { 0 };
	};

	// 编码探测请求包
	_NODISCARD Result<_STD vector<_STD uint8_t>> encodeProbePacket(const ProbePacket& packet);

	// 解码响应/公告包 (容忍旧服务端的变长布局)
	_NODISCARD Result<ProbePacket> decodeProbePacket(const _STD vector<_STD uint8_t>& data);

	// 解析主动公告包 (command=0x82)。非公告包返回 PROTOCOL_ERROR;
	// 公告包 ip 字段为空时由调用方回退为 UDP 源地址。
	_NODISCARD Result<CatalogAnnouncement> decodeAnnouncement(const _STD vector<_STD uint8_t>& data);
} // namespace plane::catalog::internal

// cy_psdk/manager/catalog/client/internal/codec/ProbePacketCodec.h
//
// SWMP UDP 探测包编解码 (对齐 java ProbePacketCodec, 与服务端字节兼容)。
//
// 包布局 (大端):
//   magic(4B)=0x53574d50 "SWMP" | version(1B)=1 | command(1B) |
//   ip(1B len + bytes) | nodeId(1B len + bytes) | status(1B) |
//   requestId(4B) | instanceId(1B len + bytes) | httpPort(2B) |
//   [可选] nodeName(1B len + bytes) | [可选] multicastAddress(1B len + bytes) | [可选] multicastPort(2B)
//
// 注意: 早期实现把 nodeName 排在 requestId 之前, 与新版服务端不符 ——
// 那会让 requestId 错位, 导致带 requestId 的响应被全部丢弃 (发现不到节点)。
// status 之后的字段全部可选, 以兼容尚未升级的旧端点 (不写 nodeName/组播)。

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
    enum ProbeCommand: ::std::uint8_t
    {
        PROBE_COMMAND          = 0X01, // 客户端探测请求
        PROBE_RESPONSE_COMMAND = 0X81, // 服务端响应
        PROBE_ANNOUNCE_COMMAND = 0X82  // 服务端主动广播公告
    };

    // 解码后的探测包内容 (对齐 java ProbePacket)
    struct ProbePacket
    {
        int           command { 0 };
        ::std::string ip {};
        ::std::string node_id {};
        int           status { 0 };
        ::std::string node_name {};
        int           request_id { 0 };
        bool          has_request_id { false };
        ::std::string instance_id {};
        int           http_port { 0 };
        // 服务端组播公告配置 (新版服务端在响应末尾追加; 旧端点不写则为空/0)
        ::std::string multicast_address {};
        int           multicast_port { 0 };
    };

    // 编码探测请求包
    [[nodiscard]] Result<::std::vector<::std::uint8_t>> encodeProbePacket(const ProbePacket& packet);

    // 解码响应/公告包 (容忍旧服务端的变长布局)
    [[nodiscard]] Result<ProbePacket> decodeProbePacket(const ::std::vector<::std::uint8_t>& data);

    // 解析主动公告包 (command=0x82)。非公告包返回 PROTOCOL_ERROR;
    // 公告包 ip 字段为空时由调用方回退为 UDP 源地址。
    [[nodiscard]] Result<CatalogAnnouncement> decodeAnnouncement(const ::std::vector<::std::uint8_t>& data);
} // namespace plane::catalog::internal

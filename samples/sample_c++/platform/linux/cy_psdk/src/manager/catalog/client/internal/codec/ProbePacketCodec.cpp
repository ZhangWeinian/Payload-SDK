// cy_psdk/manager/catalog/client/internal/codec/ProbePacketCodec.cpp

#include "manager/catalog/client/internal/codec/ProbePacketCodec.h"

#include <fmt/format.h>
#include <stdexcept>

#include "manager/catalog/client/CatalogError.h"
#include "manager/catalog/client/CatalogFailure.h"

#include "define.h"

namespace plane::catalog::internal
{
    namespace
    {
        constexpr ::std::uint32_t MAGIC     = 0X53'57'4d'50; // "SWMP"
        constexpr ::std::uint8_t  VERSION   = 0X01;
        constexpr ::std::size_t   MAX_FIELD = 255;

        void                      appendBytes(::std::vector<::std::uint8_t>& out, const ::std::string& value)
        {
            if (value.size() > MAX_FIELD)
            {
                throw ::std::invalid_argument { "packet field exceeds 255 bytes" };
            }
            out.push_back(static_cast<::std::uint8_t>(value.size()));
            out.insert(out.end(), value.begin(), value.end());
        }

        void appendU32(::std::vector<::std::uint8_t>& out, ::std::uint32_t value)
        {
            out.push_back(static_cast<::std::uint8_t>((value >> 24u) & 0Xffu));
            out.push_back(static_cast<::std::uint8_t>((value >> 16u) & 0Xffu));
            out.push_back(static_cast<::std::uint8_t>((value >> 8u) & 0Xffu));
            out.push_back(static_cast<::std::uint8_t>(value & 0Xffu));
        }

        // 追加 2 字节大端端口
        void appendPort(::std::vector<::std::uint8_t>& out, int value)
        {
            const auto unsigned_value { static_cast<::std::uint32_t>(value) };
            out.push_back(static_cast<::std::uint8_t>((unsigned_value >> 8u) & 0Xffu));
            out.push_back(static_cast<::std::uint8_t>(unsigned_value & 0Xffu));
        }

        // 读取 2 字节大端端口; 数据不足时返回 0 且不推进 offset (字段可选)
        int readPort(const ::std::vector<::std::uint8_t>& data, ::std::size_t& offset)
        {
            if (offset + 2 > data.size())
            {
                return 0;
            }
            const int value { (static_cast<int>(data[offset]) << 8) | static_cast<int>(data[offset + 1]) };
            offset += 2;
            return value;
        }

        // 读取 1 字节长度前缀 + UTF-8 字段; 越界抛 ::std::invalid_argument
        ::std::string readField(const ::std::vector<::std::uint8_t>& data, ::std::size_t& offset)
        {
            if (offset >= data.size())
            {
                throw ::std::invalid_argument { "missing field length" };
            }
            const ::std::size_t length { data[offset++] };
            if (offset + length > data.size())
            {
                throw ::std::invalid_argument { "truncated packet field" };
            }
            ::std::string value {};
            value.resize(length);
            for (::std::size_t index { 0 }; index < length; ++index)
            {
                value[index] = static_cast<char>(data[offset + index]);
            }
            offset += length;
            return value;
        }
    } // namespace

    Result<::std::vector<::std::uint8_t>> encodeProbePacket(const ProbePacket& packet)
    {
        ::std::vector<::std::uint8_t> out {};
        try
        {
            appendU32(out, MAGIC);
            out.push_back(VERSION);
            out.push_back(static_cast<::std::uint8_t>(packet.command));
            appendBytes(out, packet.ip);
            appendBytes(out, packet.node_id);
            out.push_back(static_cast<::std::uint8_t>(packet.status));
            // 布局与服务端 / java 一致: requestId -> instanceId -> httpPort -> nodeName -> 组播
            appendU32(out, static_cast<::std::uint32_t>(packet.request_id));
            appendBytes(out, packet.instance_id);
            appendPort(out, packet.http_port);
            // nodeName 放在整包末尾, 旧端点不写; 组播地址与端口追加在 nodeName 之后
            if (!packet.node_name.empty())
            {
                appendBytes(out, packet.node_name);
            }
            if (!packet.multicast_address.empty() && packet.multicast_port != 0)
            {
                appendBytes(out, packet.multicast_address);
                appendPort(out, packet.multicast_port);
            }
            return out;
        }
        catch (const ::std::exception& ex)
        {
            return ::std::unexpected(makeFailure(CatalogError::PROTOCOL_ERROR, ex.what()));
        }
    }

    Result<ProbePacket> decodeProbePacket(const ::std::vector<::std::uint8_t>& data)
    {
        if (data.size() < 8)
        {
            return ::std::unexpected(makeFailure(CatalogError::PROTOCOL_ERROR, "packet is too short"));
        }
        try
        {
            ::std::size_t offset { 0 };
            auto          readU32 = [&data, &offset]() -> ::std::uint32_t
            {
                if (offset + 4 > data.size())
                {
                    throw ::std::invalid_argument { "truncated packet field" };
                }
                const ::std::uint32_t value { (static_cast<::std::uint32_t>(data[offset]) << 24u) |
                                              (static_cast<::std::uint32_t>(data[offset + 1]) << 16u) |
                                              (static_cast<::std::uint32_t>(data[offset + 2]) << 8u) |
                                              static_cast<::std::uint32_t>(data[offset + 3]) };
                offset += 4;
                return value;
            };

            if (readU32() != MAGIC)
            {
                return ::std::unexpected(makeFailure(CatalogError::PROTOCOL_ERROR, "wrong packet magic"));
            }
            if (offset >= data.size() || data[offset++] != VERSION)
            {
                return ::std::unexpected(makeFailure(CatalogError::PROTOCOL_ERROR, "wrong packet version"));
            }
            if (offset >= data.size())
            {
                return ::std::unexpected(makeFailure(CatalogError::PROTOCOL_ERROR, "missing packet command"));
            }
            const int   command { data[offset++] };

            ProbePacket packet {};
            packet.command = command;
            packet.ip      = readField(data, offset);
            packet.node_id = readField(data, offset);
            if (offset >= data.size())
            {
                return ::std::unexpected(makeFailure(CatalogError::PROTOCOL_ERROR, "missing packet status"));
            }
            packet.status = data[offset++];

            // status 之后的字段均可选, 布局与服务端 / java 一致:
            // requestId -> instanceId -> httpPort -> nodeName -> 组播
            if (offset + 4 <= data.size())
            {
                packet.request_id     = static_cast<int>(readU32());
                packet.has_request_id = true;
            }
            if (offset < data.size())
            {
                packet.instance_id = readField(data, offset);
            }
            packet.http_port = readPort(data, offset);
            if (offset < data.size())
            {
                packet.node_name = readField(data, offset);
            }
            if (offset < data.size())
            {
                packet.multicast_address = readField(data, offset);
            }
            packet.multicast_port = readPort(data, offset);
            return packet;
        }
        catch (const ::std::exception& ex)
        {
            return ::std::unexpected(makeFailure(CatalogError::PROTOCOL_ERROR, ex.what()));
        }
    }

    Result<CatalogAnnouncement> decodeAnnouncement(const ::std::vector<::std::uint8_t>& data)
    {
        Result<ProbePacket> decoded { decodeProbePacket(data) };
        if (!decoded.has_value())
        {
            return ::std::unexpected(decoded.error());
        }
        const ProbePacket& packet { decoded.value() };
        if (packet.command != PROBE_ANNOUNCE_COMMAND)
        {
            return ::std::unexpected(
                makeFailure(CatalogError::PROTOCOL_ERROR, ::fmt::format("not an announcement packet (command=0x{:x})", packet.command))
            );
        }
        CatalogAnnouncement announcement {};
        announcement.node_id     = packet.node_id;
        announcement.ip          = packet.ip;
        announcement.node_name   = packet.node_name;
        announcement.instance_id = packet.instance_id;
        announcement.http_port   = packet.http_port;
        return announcement;
    }
} // namespace plane::catalog::internal

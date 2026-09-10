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
		constexpr _STD uint32_t MAGIC	  = 0X53'57'4d'50; // "SWMP"
		constexpr _STD uint8_t	VERSION	  = 0X01;
		constexpr _STD size_t	MAX_FIELD = 255;

		void					appendBytes(_STD vector<_STD uint8_t>& out, const _STD string& value)
		{
			if (value.size() > MAX_FIELD)
			{
				throw _STD invalid_argument { "packet field exceeds 255 bytes" };
			}
			out.push_back(static_cast<_STD uint8_t>(value.size()));
			out.insert(out.end(), value.begin(), value.end());
		}

		void appendU32(_STD vector<_STD uint8_t>& out, _STD uint32_t value)
		{
			out.push_back(static_cast<_STD uint8_t>((value >> 24) & 0Xff));
			out.push_back(static_cast<_STD uint8_t>((value >> 16) & 0Xff));
			out.push_back(static_cast<_STD uint8_t>((value >> 8) & 0Xff));
			out.push_back(static_cast<_STD uint8_t>(value & 0Xff));
		}

		// 读取 1 字节长度前缀 + UTF-8 字段; 越界抛 _STD invalid_argument
		_STD string readField(const _STD vector<_STD uint8_t>& data, _STD size_t& offset)
		{
			if (offset >= data.size())
			{
				throw _STD invalid_argument { "missing field length" };
			}
			const _STD size_t length { data[offset++] };
			if (offset + length > data.size())
			{
				throw _STD invalid_argument { "truncated packet field" };
			}
			_STD string value {};
			value.resize(length);
			for (_STD size_t index { 0 }; index < length; ++index)
			{
				value[index] = static_cast<char>(data[offset + index]);
			}
			offset += length;
			return value;
		}
	} // namespace

	Result<_STD vector<_STD uint8_t>> encodeProbePacket(const ProbePacket& packet)
	{
		_STD vector<_STD uint8_t> out {};
		try
		{
			appendU32(out, MAGIC);
			out.push_back(VERSION);
			out.push_back(static_cast<_STD uint8_t>(packet.command));
			appendBytes(out, packet.ip);
			appendBytes(out, packet.node_id);
			out.push_back(static_cast<_STD uint8_t>(packet.status));
			appendBytes(out, packet.node_name);
			appendU32(out, static_cast<_STD uint32_t>(packet.request_id));
			appendBytes(out, packet.instance_id);
			out.push_back(static_cast<_STD uint8_t>((packet.http_port >> 8) & 0Xff));
			out.push_back(static_cast<_STD uint8_t>(packet.http_port & 0Xff));
			return Result<_STD vector<_STD uint8_t>>::success(_STD move(out));
		}
		catch (const _STD exception& ex)
		{
			return Result<_STD vector<_STD uint8_t>>::failure(makeFailure(CatalogError::PROTOCOL_ERROR, ex.what()));
		}
	}

	Result<ProbePacket> decodeProbePacket(const _STD vector<_STD uint8_t>& data)
	{
		if (data.size() < 8)
		{
			return Result<ProbePacket>::failure(makeFailure(CatalogError::PROTOCOL_ERROR, "packet is too short"));
		}
		try
		{
			_STD size_t offset { 0 };
			auto readU32 = [&data, &offset]() -> _STD uint32_t
			{
				if (offset + 4 > data.size())
				{
					throw _STD invalid_argument { "truncated packet field" };
				}
				const _STD uint32_t value { (static_cast<_STD uint32_t>(data[offset]) << 24) |
											(static_cast<_STD uint32_t>(data[offset + 1]) << 16) |
											(static_cast<_STD uint32_t>(data[offset + 2]) << 8) | static_cast<_STD uint32_t>(data[offset + 3]) };
				offset += 4;
				return value;
			};

			if (readU32() != MAGIC)
			{
				return Result<ProbePacket>::failure(makeFailure(CatalogError::PROTOCOL_ERROR, "wrong packet magic"));
			}
			if (offset >= data.size() || data[offset++] != VERSION)
			{
				return Result<ProbePacket>::failure(makeFailure(CatalogError::PROTOCOL_ERROR, "wrong packet version"));
			}
			if (offset >= data.size())
			{
				return Result<ProbePacket>::failure(makeFailure(CatalogError::PROTOCOL_ERROR, "missing packet command"));
			}
			const int	command { data[offset++] };

			ProbePacket packet {};
			packet.command = command;
			packet.ip	   = readField(data, offset);
			packet.node_id = readField(data, offset);
			if (offset >= data.size())
			{
				return Result<ProbePacket>::failure(makeFailure(CatalogError::PROTOCOL_ERROR, "missing packet status"));
			}
			packet.status = data[offset++];

			// 旧服务端极短包: 无 nodeName/requestId/instanceId/port
			if (offset >= data.size())
			{
				return Result<ProbePacket>::success(packet);
			}
			packet.node_name = readField(data, offset);

			// requestId 可选 (remaining>=4)
			if (offset + 4 <= data.size())
			{
				packet.request_id	  = static_cast<int>(readU32());
				packet.has_request_id = true;
			}
			if (offset < data.size())
			{
				packet.instance_id = readField(data, offset);
			}
			if (offset + 2 <= data.size())
			{
				packet.http_port = (static_cast<int>(data[offset]) << 8) | static_cast<int>(data[offset + 1]);
			}
			return Result<ProbePacket>::success(_STD move(packet));
		}
		catch (const _STD exception& ex)
		{
			return Result<ProbePacket>::failure(makeFailure(CatalogError::PROTOCOL_ERROR, ex.what()));
		}
	}

	Result<CatalogAnnouncement> decodeAnnouncement(const _STD vector<_STD uint8_t>& data)
	{
		Result<ProbePacket> decoded { decodeProbePacket(data) };
		if (!decoded.isOk())
		{
			return Result<CatalogAnnouncement>::failure(decoded.error());
		}
		const ProbePacket& packet { decoded.value() };
		if (packet.command != PROBE_ANNOUNCE_COMMAND)
		{
			return Result<CatalogAnnouncement>::
				failure(makeFailure(CatalogError::PROTOCOL_ERROR, _FMT format("not an announcement packet (command=0x{:x})", packet.command)));
		}
		CatalogAnnouncement announcement {};
		announcement.node_id	 = packet.node_id;
		announcement.ip			 = packet.ip;
		announcement.node_name	 = packet.node_name;
		announcement.instance_id = packet.instance_id;
		announcement.http_port	 = packet.http_port;
		return Result<CatalogAnnouncement>::success(_STD move(announcement));
	}
} // namespace plane::catalog::internal

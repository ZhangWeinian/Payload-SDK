// cy_psdk 模糊测试: SWMP UDP 探测/公告报文解码 (ProbePacketCodec)
//
// 构建: cmake --preset fuzz && cmake --build --preset fuzz
// 运行: build/x86_64-linux/fuzz/bin/fuzz_probe_packet fuzz/corpus/probe_packet -runs=100000
//
// 覆盖: decodeProbePacket / decodeAnnouncement 直接接收任意字节 (解析失败返回
// Result 为预期路径, 内存安全类错误由 ASan/UBSan 暴露); 解码成功时附加
// "解码 -> 重编码 -> 再解码" 一致性回环, 顺带覆盖 encodeProbePacket。

#include <cstddef>
#include <cstdint>
#include <vector>

#include "manager/catalog/client/internal/codec/ProbePacketCodec.h"

using plane::catalog::internal::decodeAnnouncement;
using plane::catalog::internal::decodeProbePacket;
using plane::catalog::internal::encodeProbePacket;

extern "C" int LLVMFuzzerTestOneInput(const _STD uint8_t* data, _STD size_t size)
{
	_STD vector<_STD uint8_t> bytes { data, data + size };

	(void)decodeAnnouncement(bytes);

	if (const auto packet { decodeProbePacket(bytes) }; packet.has_value())
	{
		if (const auto reencoded { encodeProbePacket(packet.value()) }; reencoded.has_value())
		{
			(void)decodeProbePacket(reencoded.value());
		}
	}
	return 0;
}

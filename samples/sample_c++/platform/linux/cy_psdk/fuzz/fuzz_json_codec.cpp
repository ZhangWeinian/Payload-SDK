// cy_psdk 模糊测试: 目录 JSON 序列化/解析链路 (JsonCodec)
//
// 构建: cmake --preset fuzz && cmake --build --preset fuzz
// 运行: build/x86_64-linux/fuzz/bin/fuzz_json_codec fuzz/corpus/json_codec -runs=100000
//
// 覆盖三条生产路径:
//   1) JsonCodec::registrationToJson — 注册体构建 (含 metadata_json 内层 json::parse)
//   2) JsonCodec::statusToJson       — 状态体构建 (组件/详情子结构)
//   3) 发送路径 dump() + 传输层解析语义 — 对齐 ServiceGateway / CatalogTransport::parseRawJson
//      (parseRawJson 为私有成员, 此处以相同语义直连 nlohmann::json::parse)
//
// 说明: 序列化输入全部取自模糊数据; dump()/parse 对非法输入 (如非 UTF-8) 抛出的
// 异常按"预期拒绝"捕获。内存安全类错误 (ASan/UBSan) 不受 catch 影响, 照常暴露。

#include <cstddef>
#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include "manager/catalog/client/internal/codec/JsonCodec.h"

using plane::catalog::ExposedPort;
using plane::catalog::ServiceComponentStatus;
using plane::catalog::ServiceRegistration;
using plane::catalog::ServiceStatus;
using plane::catalog::internal::JsonCodec::registrationToJson;
using plane::catalog::internal::JsonCodec::statusToJson;

namespace
{
    // 对齐 ServiceGateway 的发送路径: 序列化结果需要 dump() 成字符串
    void dumpIfPossible(const auto& json_result)
    {
        if (!json_result.has_value())
        {
            return;
        }
        try
        {
            const _STD string dumped { json_result.value().dump() };
            (void)dumped;
        }
        catch (const _STD exception&)
        {
            // 任意字节注入字符串字段后, dump() 可能因非法 UTF-8 抛 type_error —
            // 生产侧前提是内部字符串均为合法 UTF-8, 此处按预期拒绝处理。
        }
    }
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const _STD uint8_t* data, _STD size_t size)
{
    const _STD string blob { reinterpret_cast<const char*>(data), size };

    // 1) 注册体: 字符串字段与端口信息全部注入模糊数据
    {
        ServiceRegistration registration {};
        registration.namespace_name = blob;
        registration.group_name     = blob;
        registration.service_id     = blob;
        registration.service_name   = blob;
        registration.version        = blob;
        registration.metadata_json  = blob;

        ExposedPort port {};
        port.name                  = blob;
        port.protocol              = blob;
        port.url                   = blob;
        port.port                  = size >= 2 ? static_cast<int>((data[0] << 8) | data[1]) : static_cast<int>(size);
        registration.exposed_ports = { port };

        dumpIfPossible(registrationToJson(registration, blob));
    }

    // 2) 状态体: overall_status 偶尔给合法枚举以进入深层路径
    {
        ServiceStatus status {};
        status.healthy        = size > 0 && (data[0] & 0X01u) != 0;
        status.overall_status = (size > 0 && (data[0] & 0X02u) != 0) ? _STD string { "UP" } : blob;
        status.code           = blob;
        status.message        = blob;
        if (!blob.empty())
        {
            status.details.emplace(blob, blob);
        }

        ServiceComponentStatus component {};
        component.name    = blob;
        component.status  = (size > 0 && (data[0] & 0X04u) != 0) ? _STD string { "DOWN" } : blob;
        component.code    = blob;
        component.message = blob;
        if (!blob.empty())
        {
            component.details.emplace(blob, blob);
        }
        status.components = { component };

        dumpIfPossible(statusToJson(status));
    }

    // 3) 传输层解析语义 (CatalogTransport::parseRawJson 私有; 同语义直连)
    try
    {
        const auto parsed { _NLOHMANN_JSON json::parse(blob) };
        (void)parsed;
    }
    catch (const _STD exception&)
    {}

    return 0;
}

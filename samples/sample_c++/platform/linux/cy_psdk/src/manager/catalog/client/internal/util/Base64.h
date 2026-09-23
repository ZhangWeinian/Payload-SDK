// cy_psdk/manager/catalog/client/internal/util/Base64.h
//
// 严格 Base64 解码 (SDK 内部), 用于数据池 payloadBase64 字段。
//
// 语义 (对齐 java ServiceGateway 对数据池条目的校验口径):
//   - 长度必须是 4 的倍数 (java 侧显式做同样校验, 因为 JDK 解码器容忍缺失填充);
//   - 字符必须落在标准字母表 A-Za-z0-9+/ 内;
//   - '=' 填充只允许出现在整串末尾, 且不超过 2 个。
// 任何违规都返回 PROTOCOL_ERROR —— 调用方按"服务端返回内容非法"处理, 不重试。

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "define.h"
#include "manager/catalog/client/Result.h"

namespace plane::catalog::internal
{
    // 解码标准 Base64 (含 '=' 填充)。失败返回 PROTOCOL_ERROR。
    [[nodiscard]] Result<::std::vector<::std::uint8_t>> decodeBase64(const ::std::string& text);
} // namespace plane::catalog::internal

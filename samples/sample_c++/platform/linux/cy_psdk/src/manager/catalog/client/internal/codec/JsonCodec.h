// cy_psdk/manager/catalog/client/internal/codec/JsonCodec.h
//
// 注册/状态 JSON 序列化辅助 (对齐 java JsonCodec, 用 nlohmann-json)。

#pragma once

#include <nlohmann/json.hpp>

#include "manager/catalog/client/CatalogModels.h"
#include "manager/catalog/client/Result.h"

#include "define.h"

namespace plane::catalog::internal
{
    namespace JsonCodec
    {
        // 去除首尾空白并校验 (非空, <=128 码点)
        [[nodiscard]] Result<::std::string> normalizeVersion(const ::std::string& raw);

        // 注册请求体。instance_address 为空时服务端用 HTTP 连接来源 IP 绑定。
        [[nodiscard]] Result<::nlohmann::json>
            registrationToJson(const ServiceRegistration& registration, const ::std::string& instance_address);

        // 状态上报体。overall 为空时按 healthy 生成 UP/DOWN。
        [[nodiscard]] Result<::nlohmann::json> statusToJson(const ServiceStatus& status);
    } // namespace JsonCodec
} // namespace plane::catalog::internal

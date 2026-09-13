// cy_psdk/manager/catalog/client/CatalogFailure.h
//
// 跨越 HTTP 与协议边界的完整失败信息 (对齐 java CatalogFailure record)。

#pragma once

#include <string>

#include "define.h"
#include "manager/catalog/client/CatalogError.h"

namespace plane::catalog
{
    // 完整失败信息: 客户端错误类别 + HTTP 状态 + 服务端错误码 + 消息 + 可重试性
    struct CatalogFailure
    {
        CatalogError  code { CatalogError::NONE };
        int           http_status { 0 };
        ::std::string server_code {};
        ::std::string message {};
        bool          retryable { false };
    };

    [[nodiscard]] inline CatalogFailure makeFailure(CatalogError code) noexcept
    {
        CatalogFailure failure {};
        failure.code      = code;
        failure.retryable = isRetryableError(code);
        failure.message   = ::std::string { defaultErrorMessage(code) };
        return failure;
    }

    [[nodiscard]] inline CatalogFailure makeFailure(CatalogError code, ::std::string message) noexcept
    {
        CatalogFailure failure { makeFailure(code) };
        failure.message = message.empty() ? ::std::string { defaultErrorMessage(code) } : ::std::move(message);
        return failure;
    }

    [[nodiscard]] inline CatalogFailure
        makeFailure(CatalogError code, ::std::string message, int http_status, ::std::string server_code) noexcept
    {
        CatalogFailure failure { makeFailure(code, ::std::move(message)) };
        failure.http_status = http_status;
        failure.server_code = ::std::move(server_code);
        return failure;
    }

    [[nodiscard]] inline CatalogFailure failureWithCode(CatalogFailure failure, CatalogError code) noexcept
    {
        failure.code      = code;
        failure.retryable = isRetryableError(code);
        return failure;
    }

    [[nodiscard]] inline CatalogFailure failureWithRetryable(CatalogFailure failure, bool retryable) noexcept
    {
        failure.retryable = retryable;
        return failure;
    }
} // namespace plane::catalog

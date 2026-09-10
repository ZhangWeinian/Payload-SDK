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
		CatalogError code { CatalogError::NONE };
		int			 http_status { 0 };
		_STD string	 server_code {};
		_STD string	 message {};
		bool		 retryable { false };
	};

	_NODISCARD inline CatalogFailure makeFailure(CatalogError code) noexcept
	{
		CatalogFailure failure {};
		failure.code	  = code;
		failure.retryable = isRetryableError(code);
		failure.message	  = _STD string { defaultErrorMessage(code) };
		return failure;
	}

	_NODISCARD inline CatalogFailure makeFailure(CatalogError code, _STD string message) noexcept
	{
		CatalogFailure failure { makeFailure(code) };
		failure.message = message.empty() ? _STD string { defaultErrorMessage(code) } : _STD move(message);
		return failure;
	}

	_NODISCARD inline CatalogFailure makeFailure(CatalogError code, _STD string message, int http_status, _STD string server_code) noexcept
	{
		CatalogFailure failure { makeFailure(code, _STD move(message)) };
		failure.http_status = http_status;
		failure.server_code = _STD move(server_code);
		return failure;
	}

	_NODISCARD inline CatalogFailure failureWithCode(CatalogFailure failure, CatalogError code) noexcept
	{
		failure.code	  = code;
		failure.retryable = isRetryableError(code);
		return failure;
	}

	_NODISCARD inline CatalogFailure failureWithRetryable(CatalogFailure failure, bool retryable) noexcept
	{
		failure.retryable = retryable;
		return failure;
	}
} // namespace plane::catalog

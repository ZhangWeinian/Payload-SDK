// cy_psdk/manager/catalog/client/internal/HttpTransport.h
//
// HTTP 传输抽象 (SDK 内部)。由平台实现注入 (默认 LibhvHttpTransport)。
// 语义: transport_ok=true 表示 HTTP 层传输成功 (即使服务端返回 4xx/5xx);
// transport_ok=false 表示连接/超时等传输级失败。

#pragma once

#include <chrono>
#include <string>

#include "define.h"

namespace plane::catalog::internal
{
	// HTTP 响应数据 (对齐 java HttpResponseData)
	struct HttpResponseData
	{
		int			status { 0 }; // 服务端 HTTP 状态码 (transport 失败时为 0)
		_STD string body {};
		bool		transport_ok { false };
		_STD string transport_error {};
	};

	// HTTP 传输接口 (对齐 java HttpTransport)
	class HttpTransport
	{
	public:
		virtual ~HttpTransport(void)												   = default;

		virtual HttpResponseData get(const _STD string& url)						   = 0;
		virtual HttpResponseData post(const _STD string& url, const _STD string& body) = 0;
		virtual HttpResponseData put(const _STD string& url, const _STD string& body)  = 0;
		virtual HttpResponseData del(const _STD string& url)						   = 0;

		// 连接与总请求超时 (非正数由实现按 1ms 处理)
		virtual void setTimeout(_STD_CHRONO milliseconds timeout) = 0;
	};
} // namespace plane::catalog::internal

// cy_psdk/manager/catalog/client/internal/CppHttpTransport.h
//
// 基于 cpp-httplib (vcpkg 托管) 的同步 HTTP 传输实现 (平台默认传输)。
// 每个请求使用临时 client, 天然线程安全 (Catalog 控制线程与业务线程可并发调用)。

#pragma once

#include <atomic>

#include "define.h"
#include "manager/catalog/client/internal/HttpTransport.h"

namespace plane::catalog::internal
{
	class CppHttpTransport final: public HttpTransport
	{
	public:
		CppHttpTransport(void)			 = default;
		~CppHttpTransport(void) override = default;

		HttpResponseData get(const _STD string& url) override;
		HttpResponseData post(const _STD string& url, const _STD string& body) override;
		HttpResponseData put(const _STD string& url, const _STD string& body) override;
		HttpResponseData del(const _STD string& url) override;
		void			 setTimeout(_STD_CHRONO milliseconds timeout) override;

	private:
		// 统一发送; method: 0=GET 1=POST 2=PUT 3=DELETE
		HttpResponseData sendRequest(int method, const _STD string& url, const _STD string& body);

		_STD atomic<long long> timeout_ms_ { 10'000 };
	};
} // namespace plane::catalog::internal

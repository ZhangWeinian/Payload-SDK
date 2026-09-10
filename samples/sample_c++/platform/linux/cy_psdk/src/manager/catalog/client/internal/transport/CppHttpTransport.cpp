// cy_psdk/manager/catalog/client/internal/transport/CppHttpTransport.cpp

#include "manager/catalog/client/internal/transport/CppHttpTransport.h"

#include <system_error>
#include <charconv>
#include <httplib.h>

#include "define.h"

namespace plane::catalog::internal
{
	namespace
	{
		// URL 解析 (scheme/host/port/path 由 cpp-httplib 内部解析器得出)
		struct ParsedUrl
		{
			bool		ok { false };
			_STD string host {};
			int			port { 80 };
			_STD string path { "/" };
			_STD string error {};
		};

		_NODISCARD ParsedUrl parseUrl(const _STD string& url)
		{
			ParsedUrl parsed {};
			if (url.empty())
			{
				parsed.error = "empty URL";
				return parsed;
			}

			// 解析交给 cpp-httplib 内部解析器 (detail::parse_url): scheme/host/port/path/query。
			// 说明: Client 公共构造对无效输入会静默回退 localhost:80, 不足以判错, 故直接使用内部解析结果
			_HTTPLIB detail::UrlComponents components {};
			if (!_HTTPLIB detail::parse_url(url, components) || components.host.empty())
			{
				parsed.error = "invalid URL";
				return parsed;
			}
			if (!components.scheme.empty() && components.scheme != "http")
			{
				// 目录端点仅支持明文 HTTP (https 需 SSL 编译支持, 未启用)
				parsed.error = "unsupported URL scheme: " + components.scheme;
				return parsed;
			}

			int port { 80 };
			if (!components.port.empty())
			{
				const auto result { _STD from_chars(components.port.data(), components.port.data() + components.port.size(), port) };
				if (result.ec != _STD errc {} || result.ptr != components.port.data() + components.port.size() || port <= 0 || port > 65'535)
				{
					parsed.error = "invalid URL port";
					return parsed;
				}
			}

			parsed.host	 = components.host;
			parsed.port	 = port;
			parsed.path	 = components.path.empty() ? "/" : components.path;
			parsed.path += components.query; // query 已含前导 '?'
			parsed.ok	 = true;
			return parsed;
		}
	} // namespace

	HttpResponseData CppHttpTransport::get(const _STD string& url)
	{
		return this->sendRequest(0, url, "");
	}

	HttpResponseData CppHttpTransport::post(const _STD string& url, const _STD string& body)
	{
		return this->sendRequest(1, url, body);
	}

	HttpResponseData CppHttpTransport::put(const _STD string& url, const _STD string& body)
	{
		return this->sendRequest(2, url, body);
	}

	HttpResponseData CppHttpTransport::del(const _STD string& url)
	{
		return this->sendRequest(3, url, "");
	}

	void CppHttpTransport::setTimeout(_STD_CHRONO milliseconds timeout)
	{
		const auto value { timeout.count() > 0 ? timeout.count() : 1ll };
		this->timeout_ms_.store(value, _STD memory_order_release);
	}

	HttpResponseData CppHttpTransport::sendRequest(int method, const _STD string& url, const _STD string& body)
	{
		HttpResponseData response {};

		const ParsedUrl	 parsed { parseUrl(url) };
		if (!parsed.ok)
		{
			response.transport_error = parsed.error;
			return response;
		}

		const long long timeout_ms { this->timeout_ms_.load(_STD memory_order_acquire) };

		// 每个请求使用临时 client: 线程安全且不共享连接状态
		_HTTPLIB Client client { parsed.host, parsed.port };
		client.set_connection_timeout(_STD_CHRONO milliseconds { timeout_ms });
		client.set_read_timeout(_STD_CHRONO milliseconds { timeout_ms });
		client.set_write_timeout(_STD_CHRONO milliseconds { timeout_ms });

		_HTTPLIB Result result {};
		switch (method)
		{
			case 0:
				result = client.Get(parsed.path);
				break;
			case 1:
				result = client.Post(parsed.path, body, "application/json");
				break;
			case 2:
				result = client.Put(parsed.path, body, "application/json");
				break;
			case 3:
				result = client.Delete(parsed.path);
				break;
			default:
				response.transport_error = "invalid method";
				return response;
		}

		if (result.error() != _HTTPLIB Error::Success)
		{
			response.transport_error = _HTTPLIB to_string(result.error());
			return response;
		}

		response.transport_ok = true;
		response.status		  = result->status;
		response.body		  = result->body;
		return response;
	}
} // namespace plane::catalog::internal

// cy_psdk/manager/catalog/client/internal/CppHttpTransport.cpp

#include "manager/catalog/client/internal/CppHttpTransport.h"

#include <httplib.h>

#include "define.h"

namespace plane::catalog::internal
{
	namespace
	{
		// 从完整 URL 拆出 host/port/path (仅支持 http://host[:port]/path, 目录为 IPv4 HTTP 端点)
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

			_STD string rest { url };
			if (rest.rfind("http://", 0) == 0)
			{
				rest = rest.substr(7);
			}
			else if (rest.rfind("https://", 0) == 0)
			{
				parsed.error = "https 目录端点暂不支持";
				return parsed;
			}

			// 取主机段: 到第一个 '/' 或结束
			const _STD size_t slash { rest.find('/') };
			_STD string		  authority { slash == _STD string::npos ? rest : rest.substr(0, slash) };
			parsed.path = slash == _STD string::npos ? "/" : rest.substr(slash);
			if (authority.empty())
			{
				parsed.error = "invalid URL host";
				return parsed;
			}

			// host[:port]
			const _STD size_t colon { authority.rfind(':') };
			if (colon != _STD string::npos)
			{
				parsed.host = authority.substr(0, colon);
				try
				{
					parsed.port = _STD stoi(authority.substr(colon + 1));
				}
				catch (const _STD exception&)
				{
					parsed.error = "invalid URL port";
					return parsed;
				}
				if (parsed.port <= 0 || parsed.port > 65'535)
				{
					parsed.error = "invalid URL port";
					return parsed;
				}
			}
			else
			{
				parsed.host = authority;
			}
			parsed.ok = true;
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

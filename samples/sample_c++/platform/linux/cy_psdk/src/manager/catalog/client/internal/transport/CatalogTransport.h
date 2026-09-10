// cy_psdk/manager/catalog/client/internal/transport/CatalogTransport.h
//
// 目录 HTTP 传输封装: URL 基址 + 远程访问门控 (remoteAllowed/conflict/stopping)
// + HTTP 状态到 CatalogError 的映射 (对齐 java CatalogTransport)。

#pragma once

#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>

#include "define.h"
#include "manager/catalog/client/CatalogFailure.h"
#include "manager/catalog/client/internal/transport/HttpTransport.h"
#include "manager/catalog/client/Result.h"

namespace plane::catalog::internal
{
	class CatalogTransport
	{
	public:
		// http 为空时使用默认 CppHttpTransport 实现
		CatalogTransport(_STD string base_url, _STD unique_ptr<HttpTransport> http);
		~CatalogTransport(void)									  = default;

		CatalogTransport(const CatalogTransport&)				  = delete;
		CatalogTransport&	   operator=(const CatalogTransport&) = delete;

		_NODISCARD _STD string catalogUrl(void) const
		{
			_STD lock_guard<_STD mutex> lock { this->url_mutex_ };
			return this->base_url_;
		}

		void setCatalogUrl(_STD string value)
		{
			_STD lock_guard<_STD mutex> lock { this->url_mutex_ };
			this->base_url_ = trimSlash(_STD move(value));
		}

		void setTimeout(_STD_CHRONO milliseconds timeout)
		{
			this->http_->setTimeout(timeout);
		}

		void setRemoteAllowed(bool value)
		{
			this->remote_allowed_.store(value, _STD memory_order_release);
		}

		void setConflict(bool value)
		{
			this->conflict_.store(value, _STD memory_order_release);
		}

		void setStopping(bool value)
		{
			this->stopping_.store(value, _STD memory_order_release);
		}

		_NODISCARD Result<_NLOHMANN_JSON json> getJson(const _STD string& path);
		_NODISCARD							   Result<_NLOHMANN_JSON json>
				   postJson(const _STD string& path, const _STD string& body, bool accept_idempotent_register = false);
		_NODISCARD Result<_NLOHMANN_JSON json> putJson(const _STD string& path, const _STD string& body);
		_NODISCARD Result<void> postVoid(const _STD string& path, const _STD string& body);
		_NODISCARD Result<void> putVoid(const _STD string& path, const _STD string& body);
		_NODISCARD Result<void> del(const _STD string& path);

	private:
		_NODISCARD Result<void> gate(void) const;
		_NODISCARD Result<_NLOHMANN_JSON json> parse(const HttpResponseData& response, bool empty_object_allowed) const;
		_NODISCARD Result<_NLOHMANN_JSON json> parseRawJson(const _STD string& body, int status) const;
		_NODISCARD _STD optional<CatalogFailure> mapHttp(const HttpResponseData& response) const;
		_NODISCARD _STD string					 fullUrl(const _STD string& path) const;

		static _STD string						 trimSlash(_STD string value);

		_STD unique_ptr<HttpTransport> http_ {};
		_STD string					   base_url_ {};
		mutable _STD mutex			   url_mutex_ {};
		_STD atomic<bool> remote_allowed_ { true };
		_STD atomic<bool> conflict_ { false };
		_STD atomic<bool> stopping_ { false };
	};
} // namespace plane::catalog::internal

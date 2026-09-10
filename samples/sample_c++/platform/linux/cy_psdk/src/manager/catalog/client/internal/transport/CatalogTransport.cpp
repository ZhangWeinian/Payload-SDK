// cy_psdk/manager/catalog/client/internal/transport/CatalogTransport.cpp

#include "manager/catalog/client/internal/transport/CatalogTransport.h"

#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>

#include "manager/catalog/client/CatalogError.h"
#include "manager/catalog/client/internal/transport/CppHttpTransport.h"

#include "define.h"

namespace plane::catalog::internal
{
	namespace
	{
		// 服务端返回的错误码 (识别后作为协议错误透出 server_code)
		_NODISCARD bool isRecognizedServerCode(const _STD string& code)
		{
			static const _STD set<_STD string> kRecognized {
				"NOT_FOUND",		"BAD_REQUEST",			 "METHOD_NOT_ALLOWED", "CONFLICT",
				"SERVICE_CONFLICT", "SERVICE_NAME_CONFLICT", "INTERNAL_ERROR",	   "UNAVAILABLE",
			};
			return kRecognized.find(code) != kRecognized.end();
		}

		struct ServerError
		{
			_STD string code {};
			_STD string message {};
			bool		recognized { false };
		};

		_NODISCARD ServerError parseServerError(const _STD string& body)
		{
			if (body.empty())
			{
				return {};
			}
			try
			{
				const _NLOHMANN_JSON json json = _NLOHMANN_JSON json::parse(body);
				if (!json.is_object() || !json.contains("code") || json["code"].is_null())
				{
					return {};
				}
				if (!json["code"].is_string())
				{
					return { "", "invalid type: code", true };
				}
				const _STD string code { json["code"].get<_STD string>() };
				_STD string		  message { code };
				if (json.contains("message") && json["message"].is_string())
				{
					message = json["message"].get<_STD string>();
				}
				return { code, message, isRecognizedServerCode(code) };
			}
			catch (const _STD exception& ex)
			{
				return { "", ex.what(), false };
			}
		}
	} // namespace

	CatalogTransport::CatalogTransport(_STD string base_url, _STD unique_ptr<HttpTransport> http):
		http_(http ? _STD move(http) : _STD make_unique<CppHttpTransport>()),
		base_url_(trimSlash(_STD move(base_url)))
	{
		this->http_->setTimeout(_STD_CHRONO milliseconds { 10'000 });
	}

	_NODISCARD _STD string CatalogTransport::trimSlash(_STD string value)
	{
		while (!value.empty() && value.back() == '/')
		{
			value.pop_back();
		}
		return value;
	}

	_NODISCARD _STD string CatalogTransport::fullUrl(const _STD string& path) const
	{
		if (path.empty())
		{
			return this->catalogUrl();
		}
		return this->catalogUrl() + (path.front() == '/' ? path : "/" + path);
	}

	_NODISCARD Result<void> CatalogTransport::gate(void) const
	{
		if (this->stopping_.load(_STD memory_order_acquire))
		{
			return Result<void>::failure(makeFailure(CatalogError::STOPPED));
		}
		if (this->conflict_.load(_STD memory_order_acquire))
		{
			return Result<void>::failure(makeFailure(CatalogError::CATALOG_CONFLICT));
		}
		if (!this->remote_allowed_.load(_STD memory_order_acquire))
		{
			return Result<void>::failure(makeFailure(CatalogError::CATALOG_UNAVAILABLE));
		}
		if (this->catalogUrl().empty())
		{
			return Result<void>::failure(makeFailure(CatalogError::INVALID_ARGUMENT, "empty URL"));
		}
		return Result<void>::success();
	}

	_NODISCARD _STD optional<CatalogFailure> CatalogTransport::mapHttp(const HttpResponseData& response) const
	{
		if (!response.transport_ok)
		{
			CatalogFailure failure { makeFailure(
				CatalogError::CATALOG_UNAVAILABLE,
				response.transport_error.empty() ? _STD string { defaultErrorMessage(CatalogError::CATALOG_UNAVAILABLE) }
												 : response.transport_error
			) };
			return failure;
		}

		const ServerError server { parseServerError(response.body) };
		if (response.status >= 200 && response.status < 300)
		{
			if (server.recognized)
			{
				return makeFailure(CatalogError::PROTOCOL_ERROR, server.message, response.status, server.code);
			}
			return _STD nullopt; // 成功
		}

		CatalogError code { CatalogError::HTTP_ERROR };
		bool		 retryable { false };
		if (response.status == 408)
		{
			code	  = CatalogError::TIMEOUT;
			retryable = true;
		}
		else if (response.status == 404)
		{
			code = CatalogError::SERVICE_NOT_FOUND;
		}
		else if (response.status == 400 || response.status == 409)
		{
			code = CatalogError::REGISTRATION_REJECTED;
		}
		else if (response.status == 429 || response.status >= 500)
		{
			code	  = CatalogError::CATALOG_UNAVAILABLE;
			retryable = true;
		}

		_STD string message { server.message };
		if (message.empty())
		{
			message = response.body.empty() ? _FMT format("HTTP {}", response.status) : response.body;
		}
		CatalogFailure failure { makeFailure(code, _STD move(message), response.status, server.code) };
		failure.retryable = retryable;
		return failure;
	}

	_NODISCARD Result<_NLOHMANN_JSON json> CatalogTransport::parseRawJson(const _STD string& body, int status) const
	{
		try
		{
			return Result<_NLOHMANN_JSON json>::success(_NLOHMANN_JSON json::parse(body));
		}
		catch (const _STD exception& ex)
		{
			return Result<_NLOHMANN_JSON json>::failure(makeFailure(CatalogError::PROTOCOL_ERROR, ex.what(), status, ""));
		}
	}

	_NODISCARD Result<_NLOHMANN_JSON json> CatalogTransport::parse(const HttpResponseData& response, bool empty_object_allowed) const
	{
		const _STD optional<CatalogFailure> failure { this->mapHttp(response) };
		if (failure.has_value())
		{
			return Result<_NLOHMANN_JSON json>::failure(*failure);
		}
		if (response.body.empty())
		{
			if (empty_object_allowed)
			{
				return Result<_NLOHMANN_JSON json>::success(_NLOHMANN_JSON json::object());
			}
			return Result<_NLOHMANN_JSON json>::failure(makeFailure(CatalogError::PROTOCOL_ERROR, "empty JSON body", response.status, ""));
		}
		return this->parseRawJson(response.body, response.status);
	}

	_NODISCARD Result<_NLOHMANN_JSON json> CatalogTransport::getJson(const _STD string& path)
	{
		Result<void> check { this->gate() };
		if (!check.isOk())
		{
			return Result<_NLOHMANN_JSON json>::failure(check.error());
		}
		return this->parse(this->http_->get(this->fullUrl(path)), false);
	}

	_NODISCARD Result<_NLOHMANN_JSON json>
			   CatalogTransport::postJson(const _STD string& path, const _STD string& body, bool accept_idempotent_register)
	{
		Result<void> check { this->gate() };
		if (!check.isOk())
		{
			return Result<_NLOHMANN_JSON json>::failure(check.error());
		}
		const HttpResponseData response { this->http_->post(this->fullUrl(path), body) };

		if (accept_idempotent_register && response.transport_ok && response.status == 409)
		{
			// 注册冲突: 若服务端判定幂等 (已注册过, 携带 id) 则视为成功
			Result<_NLOHMANN_JSON json> parsed { this->parseRawJson(response.body, response.status) };
			if (!parsed.isOk())
			{
				return parsed;
			}
			const _NLOHMANN_JSON json json = parsed.value();
			if (!json.is_object())
			{
				return Result<_NLOHMANN_JSON json>::
					failure(makeFailure(CatalogError::PROTOCOL_ERROR, "registration conflict response must be an object", response.status, ""));
			}
			static const _STD set<_STD string> kIdempotent { "ALREADY_REGISTERED", "ALREADY_EXISTS", "IDEMPOTENT", "DUPLICATE" };
			_STD string						   code {};
			if (json.contains("code") && !json["code"].is_null())
			{
				if (!json["code"].is_string())
				{
					return Result<_NLOHMANN_JSON json>::
						failure(makeFailure(CatalogError::PROTOCOL_ERROR, "invalid type: code", response.status, ""));
				}
				code = json["code"].get<_STD string>();
			}
			const bool has_id { json.contains("id") && json["id"].is_string() && !json["id"].get<_STD string>().empty() };
			if (kIdempotent.find(code) != kIdempotent.end() || has_id)
			{
				return Result<_NLOHMANN_JSON json>::success(json);
			}
			CatalogFailure failure { failureWithCode(this->mapHttp(response).value_or(CatalogFailure {}), CatalogError::REGISTRATION_REJECTED) };
			failure.retryable = false;
			return Result<_NLOHMANN_JSON json>::failure(failure);
		}
		return this->parse(response, false);
	}

	_NODISCARD Result<_NLOHMANN_JSON json> CatalogTransport::putJson(const _STD string& path, const _STD string& body)
	{
		Result<void> check { this->gate() };
		if (!check.isOk())
		{
			return Result<_NLOHMANN_JSON json>::failure(check.error());
		}
		return this->parse(this->http_->put(this->fullUrl(path), body), true);
	}

	_NODISCARD Result<void> CatalogTransport::postVoid(const _STD string& path, const _STD string& body)
	{
		Result<void> check { this->gate() };
		if (!check.isOk())
		{
			return check;
		}
		const _STD optional<CatalogFailure> failure { this->mapHttp(this->http_->post(this->fullUrl(path), body)) };
		if (failure.has_value())
		{
			return Result<void>::failure(*failure);
		}
		return Result<void>::success();
	}

	_NODISCARD Result<void> CatalogTransport::putVoid(const _STD string& path, const _STD string& body)
	{
		Result<void> check { this->gate() };
		if (!check.isOk())
		{
			return check;
		}
		const _STD optional<CatalogFailure> failure { this->mapHttp(this->http_->put(this->fullUrl(path), body)) };
		if (failure.has_value())
		{
			return Result<void>::failure(*failure);
		}
		return Result<void>::success();
	}

	_NODISCARD Result<void> CatalogTransport::del(const _STD string& path)
	{
		Result<void> check { this->gate() };
		if (!check.isOk())
		{
			return check;
		}
		const _STD optional<CatalogFailure> failure { this->mapHttp(this->http_->del(this->fullUrl(path))) };
		if (failure.has_value())
		{
			return Result<void>::failure(*failure);
		}
		return Result<void>::success();
	}
} // namespace plane::catalog::internal

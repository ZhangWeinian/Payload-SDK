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
        [[nodiscard]] bool isRecognizedServerCode(const ::std::string& code)
        {
            static const ::std::set<::std::string> kRecognized {
                "NOT_FOUND",        "BAD_REQUEST",           "METHOD_NOT_ALLOWED", "CONFLICT",
                "SERVICE_CONFLICT", "SERVICE_NAME_CONFLICT", "INTERNAL_ERROR",     "UNAVAILABLE",
            };
            return kRecognized.find(code) != kRecognized.end();
        }

        struct ServerError
        {
            ::std::string code {};
            ::std::string message {};
            bool          recognized { false };
        };

        [[nodiscard]] ServerError parseServerError(const ::std::string& body)
        {
            if (body.empty())
            {
                return {};
            }
            try
            {
                const ::nlohmann::json json = ::nlohmann::json::parse(body);
                if (!json.is_object() || !json.contains("code") || json["code"].is_null())
                {
                    return {};
                }
                if (!json["code"].is_string())
                {
                    return { "", "invalid type: code", true };
                }
                const ::std::string code { json["code"].get<::std::string>() };
                ::std::string       message { code };
                if (json.contains("message") && json["message"].is_string())
                {
                    message = json["message"].get<::std::string>();
                }
                return { code, message, isRecognizedServerCode(code) };
            }
            catch (const ::std::exception& ex)
            {
                return { "", ex.what(), false };
            }
        }
    } // namespace

    CatalogTransport::CatalogTransport(
        ::std::string                    base_url,
        ::std::unique_ptr<HttpTransport> http
    ): http_(http ? ::std::move(http) : ::std::make_unique<CppHttpTransport>()),
       base_url_(trimSlash(::std::move(base_url)))
    {
        this->http_->setTimeout(::std::chrono::milliseconds { 10'000 });
    }

    [[nodiscard]] ::std::string CatalogTransport::trimSlash(::std::string value)
    {
        while (!value.empty() && value.back() == '/')
        {
            value.pop_back();
        }
        return value;
    }

    [[nodiscard]] ::std::string CatalogTransport::fullUrl(const ::std::string& path) const
    {
        if (path.empty())
        {
            return this->catalogUrl();
        }
        return this->catalogUrl() + (path.front() == '/' ? path : "/" + path);
    }

    [[nodiscard]] Result<void> CatalogTransport::gate(void) const
    {
        if (this->stopping_.load(::std::memory_order_acquire))
        {
            return ::std::unexpected(makeFailure(CatalogError::STOPPED));
        }
        if (this->conflict_.load(::std::memory_order_acquire))
        {
            return ::std::unexpected(makeFailure(CatalogError::CATALOG_CONFLICT));
        }
        if (!this->remote_allowed_.load(::std::memory_order_acquire))
        {
            return ::std::unexpected(makeFailure(CatalogError::CATALOG_UNAVAILABLE));
        }
        if (this->catalogUrl().empty())
        {
            return ::std::unexpected(makeFailure(CatalogError::INVALID_ARGUMENT, "empty URL"));
        }
        return {};
    }

    [[nodiscard]] ::std::optional<CatalogFailure> CatalogTransport::mapHttp(const HttpResponseData& response) const
    {
        if (!response.transport_ok)
        {
            CatalogFailure failure { makeFailure(
                CatalogError::CATALOG_UNAVAILABLE,
                response.transport_error.empty() ? ::std::string { defaultErrorMessage(CatalogError::CATALOG_UNAVAILABLE) }
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
            return ::std::nullopt; // 成功
        }

        CatalogError code { CatalogError::HTTP_ERROR };
        bool         retryable { false };
        if (response.status == 408)
        {
            code      = CatalogError::TIMEOUT;
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
            code      = CatalogError::CATALOG_UNAVAILABLE;
            retryable = true;
        }

        ::std::string message { server.message };
        if (message.empty())
        {
            message = response.body.empty() ? ::fmt::format("HTTP {}", response.status) : response.body;
        }
        CatalogFailure failure { makeFailure(code, ::std::move(message), response.status, server.code) };
        failure.retryable = retryable;
        return failure;
    }

    [[nodiscard]] Result<::nlohmann::json> CatalogTransport::parseRawJson(const ::std::string& body, int status) const
    {
        try
        {
            return ::nlohmann::json::parse(body);
        }
        catch (const ::std::exception& ex)
        {
            return ::std::unexpected(makeFailure(CatalogError::PROTOCOL_ERROR, ex.what(), status, ""));
        }
    }

    [[nodiscard]] Result<::nlohmann::json> CatalogTransport::parse(const HttpResponseData& response, bool empty_object_allowed) const
    {
        const ::std::optional<CatalogFailure> failure { this->mapHttp(response) };
        if (failure.has_value())
        {
            return ::std::unexpected(*failure);
        }
        if (response.body.empty())
        {
            if (empty_object_allowed)
            {
                return ::nlohmann::json::object();
            }
            return ::std::unexpected(makeFailure(CatalogError::PROTOCOL_ERROR, "empty JSON body", response.status, ""));
        }
        return this->parseRawJson(response.body, response.status);
    }

    [[nodiscard]] Result<::nlohmann::json> CatalogTransport::getJson(const ::std::string& path)
    {
        Result<void> check { this->gate() };
        if (!check.has_value())
        {
            return ::std::unexpected(check.error());
        }
        return this->parse(this->http_->get(this->fullUrl(path)), false);
    }

    [[nodiscard]] Result<::nlohmann::json>
        CatalogTransport::postJson(const ::std::string& path, const ::std::string& body, bool accept_idempotent_register)
    {
        Result<void> check { this->gate() };
        if (!check.has_value())
        {
            return ::std::unexpected(check.error());
        }
        const HttpResponseData response { this->http_->post(this->fullUrl(path), body) };

        if (accept_idempotent_register && response.transport_ok && response.status == 409)
        {
            // 注册冲突: 若服务端判定幂等 (已注册过, 携带 id) 则视为成功
            Result<::nlohmann::json> parsed { this->parseRawJson(response.body, response.status) };
            if (!parsed.has_value())
            {
                return parsed;
            }
            const ::nlohmann::json json = parsed.value();
            if (!json.is_object())
            {
                return ::std::unexpected(
                    makeFailure(CatalogError::PROTOCOL_ERROR, "registration conflict response must be an object", response.status, "")
                );
            }
            static const ::std::set<::std::string> kIdempotent { "ALREADY_REGISTERED", "ALREADY_EXISTS", "IDEMPOTENT", "DUPLICATE" };
            ::std::string                          code {};
            if (json.contains("code") && !json["code"].is_null())
            {
                if (!json["code"].is_string())
                {
                    return ::std::unexpected(makeFailure(CatalogError::PROTOCOL_ERROR, "invalid type: code", response.status, ""));
                }
                code = json["code"].get<::std::string>();
            }
            const bool has_id { json.contains("id") && json["id"].is_string() && !json["id"].get<::std::string>().empty() };
            if (kIdempotent.find(code) != kIdempotent.end() || has_id)
            {
                return json;
            }
            CatalogFailure failure { failureWithCode(this->mapHttp(response).value_or(CatalogFailure {}), CatalogError::REGISTRATION_REJECTED) };
            failure.retryable = false;
            return ::std::unexpected(failure);
        }
        return this->parse(response, false);
    }

    [[nodiscard]] Result<::nlohmann::json> CatalogTransport::putJson(const ::std::string& path, const ::std::string& body)
    {
        Result<void> check { this->gate() };
        if (!check.has_value())
        {
            return ::std::unexpected(check.error());
        }
        return this->parse(this->http_->put(this->fullUrl(path), body), true);
    }

    [[nodiscard]] Result<void> CatalogTransport::postVoid(const ::std::string& path, const ::std::string& body)
    {
        Result<void> check { this->gate() };
        if (!check.has_value())
        {
            return check;
        }
        const ::std::optional<CatalogFailure> failure { this->mapHttp(this->http_->post(this->fullUrl(path), body)) };
        if (failure.has_value())
        {
            return ::std::unexpected(*failure);
        }
        return {};
    }

    [[nodiscard]] Result<void> CatalogTransport::putVoid(const ::std::string& path, const ::std::string& body)
    {
        Result<void> check { this->gate() };
        if (!check.has_value())
        {
            return check;
        }
        const ::std::optional<CatalogFailure> failure { this->mapHttp(this->http_->put(this->fullUrl(path), body)) };
        if (failure.has_value())
        {
            return ::std::unexpected(*failure);
        }
        return {};
    }

    [[nodiscard]] Result<void> CatalogTransport::del(const ::std::string& path)
    {
        Result<void> check { this->gate() };
        if (!check.has_value())
        {
            return check;
        }
        const ::std::optional<CatalogFailure> failure { this->mapHttp(this->http_->del(this->fullUrl(path))) };
        if (failure.has_value())
        {
            return ::std::unexpected(*failure);
        }
        return {};
    }
} // namespace plane::catalog::internal

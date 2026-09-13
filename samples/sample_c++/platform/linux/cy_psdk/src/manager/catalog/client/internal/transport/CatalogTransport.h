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
        CatalogTransport(::std::string base_url, ::std::unique_ptr<HttpTransport> http);
        ~CatalogTransport(void)                                        = default;

        CatalogTransport(const CatalogTransport&)                      = delete;
        CatalogTransport&           operator=(const CatalogTransport&) = delete;

        [[nodiscard]] ::std::string catalogUrl(void) const
        {
            ::std::lock_guard<::std::mutex> lock { this->url_mutex_ };
            return this->base_url_;
        }

        void setCatalogUrl(::std::string value)
        {
            ::std::lock_guard<::std::mutex> lock { this->url_mutex_ };
            this->base_url_ = trimSlash(::std::move(value));
        }

        void setTimeout(::std::chrono::milliseconds timeout)
        {
            this->http_->setTimeout(timeout);
        }

        void setRemoteAllowed(bool value)
        {
            this->remote_allowed_.store(value, ::std::memory_order_release);
        }

        void setConflict(bool value)
        {
            this->conflict_.store(value, ::std::memory_order_release);
        }

        void setStopping(bool value)
        {
            this->stopping_.store(value, ::std::memory_order_release);
        }

        [[nodiscard]] Result<::nlohmann::json> getJson(const ::std::string& path);
        [[nodiscard]] Result<::nlohmann::json>
            postJson(const ::std::string& path, const ::std::string& body, bool accept_idempotent_register = false);
        [[nodiscard]] Result<::nlohmann::json> putJson(const ::std::string& path, const ::std::string& body);
        [[nodiscard]] Result<void>             postVoid(const ::std::string& path, const ::std::string& body);
        [[nodiscard]] Result<void>             putVoid(const ::std::string& path, const ::std::string& body);
        [[nodiscard]] Result<void>             del(const ::std::string& path);

    private:
        [[nodiscard]] Result<void>                    gate(void) const;
        [[nodiscard]] Result<::nlohmann::json>        parse(const HttpResponseData& response, bool empty_object_allowed) const;
        [[nodiscard]] Result<::nlohmann::json>        parseRawJson(const ::std::string& body, int status) const;
        [[nodiscard]] ::std::optional<CatalogFailure> mapHttp(const HttpResponseData& response) const;
        [[nodiscard]] ::std::string                   fullUrl(const ::std::string& path) const;

        static ::std::string                          trimSlash(::std::string value);

        ::std::unique_ptr<HttpTransport>              http_ {};
        ::std::string                                 base_url_ {};
        mutable ::std::mutex                          url_mutex_ {};
        ::std::atomic<bool>                           remote_allowed_ { true };
        ::std::atomic<bool>                           conflict_ { false };
        ::std::atomic<bool>                           stopping_ { false };
    };
} // namespace plane::catalog::internal

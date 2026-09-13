// cy_psdk/manager/catalog/client/internal/state/ConfigCache.h
//
// 单项配置的最后一次有效缓存 (对齐 java ConfigCache)。线程安全。

#pragma once

#include <map>
#include <mutex>
#include <optional>

#include "define.h"
#include "manager/catalog/client/CatalogModels.h"

namespace plane::catalog::internal
{
    class ConfigCache
    {
    public:
        ConfigCache(void) = default;

        void remember(const ConfigKey& key, const ConfigDocument& document)
        {
            ::std::lock_guard<::std::mutex> lock { this->mutex_ };
            this->documents_[key] = document;
        }

        // 返回 from_cache=true 的缓存副本
        ::std::optional<ConfigDocument> cached(const ConfigKey& key) const
        {
            ::std::lock_guard<::std::mutex> lock { this->mutex_ };
            const auto                      it { this->documents_.find(key) };
            if (it == this->documents_.end())
            {
                return ::std::nullopt;
            }
            ConfigDocument document { it->second };
            document.from_cache = true;
            return document;
        }

        // 返回当前缓存 (不做 from_cache 标记)
        ::std::optional<ConfigDocument> current(const ConfigKey& key) const
        {
            ::std::lock_guard<::std::mutex> lock { this->mutex_ };
            const auto                      it { this->documents_.find(key) };
            if (it == this->documents_.end())
            {
                return ::std::nullopt;
            }
            return it->second;
        }

    private:
        mutable ::std::mutex                  mutex_ {};
        ::std::map<ConfigKey, ConfigDocument> documents_ {};
    };
} // namespace plane::catalog::internal

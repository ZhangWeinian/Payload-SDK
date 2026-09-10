// cy_psdk/manager/catalog/client/internal/discovery/CatalogIpCache.h
//
// 上次成功 Catalog IP 缓存 (对齐 java CatalogIpCache)。仅在使用时持久化到文件;
// 为空路径时禁用 (psdk 默认不启用, 每次重新探测)。

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "define.h"

namespace plane::catalog::internal
{
	class CatalogIpCache
	{
	public:
		explicit CatalogIpCache(_STD string file);
		~CatalogIpCache(void) = default;

		// 若缓存 IP 在 targets 中, 移到首位优先探测
		_NODISCARD _STD vector<_STD string> prioritize(const _STD vector<_STD string>& targets);

		// 原子写入缓存 IP (非规范 IPv4 时忽略)
		void save(const _STD string& ip);

	private:
		_STD string file_ {};
	};
} // namespace plane::catalog::internal

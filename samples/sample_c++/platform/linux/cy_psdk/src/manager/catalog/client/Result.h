// cy_psdk/manager/catalog/client/Result.h
//
// 值或失败的结果封装 (对齐 java Result<T>): C++23 起直接采用标准库
// std::expected<T, CatalogFailure>, 保留 Result 名称作为领域别名。
//
// 迁移映射 (相对旧自研 Result):
//   Result<T>::success(v)   -> 直接返回 v (expected 隐式构造)
//   Result<T>::failure(e)   -> std::unexpected(e)
//   result.isOk()           -> result.has_value()
//   result.value()          -> result.value()   (失败时抛 std::bad_expected_access)
//   result.error()          -> result.error()   (前提: has_value() 为 false)

#pragma once

#include <expected>

#include "define.h"
#include "manager/catalog/client/CatalogFailure.h"

namespace plane::catalog
{
	// 通用结果: 成功携带 T, 失败携带 CatalogFailure (标准库 std::expected 别名)
	template<typename T>
	using Result = _STD expected<T, CatalogFailure>;
} // namespace plane::catalog

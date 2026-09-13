// cy_psdk/manager/catalog/client/internal/util/TargetExpander.h
//
// 探测目标展开 (对齐 java TargetExpander)。支持单 IP / 末段通配 .* / CIDR / 起止范围。

#pragma once

#include <string>
#include <vector>

#include "define.h"
#include "manager/catalog/client/Result.h"

namespace plane::catalog::internal
{
    // 展开探测目标列表; 单个规格非法或结果超过 max_count 时返回 INVALID_ARGUMENT。
    [[nodiscard]] Result<::std::vector<::std::string>> expandTargets(const ::std::vector<::std::string>& specs, int max_count);
} // namespace plane::catalog::internal

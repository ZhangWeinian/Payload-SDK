// cy_psdk/protocol/UrgentReport.h

#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "define.h"

namespace plane::protocol
{
	using n_json = _NLOHMANN_JSON json;

	struct EventReportPayload
	{
		int			SJDJ {}; // 事件等级
		int			SJM {};	 // 事件码
		int			SJBT {}; // 事件标题
		_STD string SJXQ {}; // 事件详情
	};

	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(EventReportPayload, SJDJ, SJM, SJBT, SJXQ);
} // namespace plane::protocol

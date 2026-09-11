// cy_psdk/utils/rtsp_util/RtspUrl.h

#pragma once

#include <string>

#include "define.h"
#include "domain/PlaneStateDataClass.h"

namespace plane::utils
{
	// 组装本机 RTSP 推流地址 "rtsp://user:pass@<本机IP>:port/base"。
	_NODISCARD _STD string buildLocalRtspUrl(const plane::domain::PlaneStateDataClass& snapshot) noexcept;
} // namespace plane::utils

// cy_psdk/utils/rtsp_util/RtspUrl.h

#pragma once

#include <string>

#include "define.h"
#include "domain/PlaneStateDataClass.h"

namespace plane::utils
{
    // 组装本机 RTSP 推流地址 "rtsp://user:pass@<本机IP>:port/base"。
    // 字段级读取路径: 调用方逐字段取值后传入 (高频读取方避免整份快照)
    [[nodiscard]] ::std::string
        buildLocalRtspUrl(::std::string_view user, ::std::string_view password, ::std::string_view base_url, int server_port) noexcept;

    // 快照路径: 调用方已持有整份状态快照时使用
    [[nodiscard]] ::std::string buildLocalRtspUrl(const plane::domain::PlaneStateDataClass& snapshot) noexcept;
} // namespace plane::utils

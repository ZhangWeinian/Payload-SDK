// cy_psdk/utils/rtsp_util/RtspUrl.cpp

#include "utils/rtsp_util/RtspUrl.h"

#include <fmt/format.h>

#include "utils/log_util/Logger.h"
#include "utils/network_util/GetLocalIPV4.h"

namespace plane::utils
{
    ::std::string buildLocalRtspUrl(::std::string_view user, ::std::string_view password, ::std::string_view base_url, int server_port) noexcept
    {
        const auto local_ip { plane::utils::getLocalIPV4() };
        if (!local_ip.has_value() || local_ip->empty())
        {
            LOG_DEBUG("本机 IP 未就绪, RTSP 地址留空");
            return {};
        }

        if (user.empty() || password.empty() || base_url.empty() || server_port <= 0)
        {
            LOG_DEBUG("RTSP 推流配置不完整 (user/pass/port/base), 地址留空");
            return {};
        }

        return ::fmt::format("rtsp://{}:{}@{}:{}/{}", user, password, *local_ip, server_port, base_url);
    }

    ::std::string buildLocalRtspUrl(const plane::domain::PlaneStateDataClass& snapshot) noexcept
    {
        return buildLocalRtspUrl(
            snapshot.rtsp_push_video_user_name,
            snapshot.rtsp_push_video_password,
            snapshot.rtsp_push_video_base_url,
            snapshot.rtsp_push_video_server_port
        );
    }
} // namespace plane::utils

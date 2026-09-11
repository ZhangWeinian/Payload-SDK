// cy_psdk/utils/rtsp_util/RtspUrl.cpp

#include "utils/rtsp_util/RtspUrl.h"

#include <fmt/format.h>

#include "utils/log_util/Logger.h"
#include "utils/network_util/GetLocalIPV4.h"

namespace plane::utils
{
	_STD string buildLocalRtspUrl(const plane::domain::PlaneStateDataClass& snapshot) noexcept
	{
		const auto local_ip { plane::utils::getLocalIPV4() };
		if (!local_ip.has_value() || local_ip->empty())
		{
			LOG_DEBUG("本机 IP 未就绪, RTSP 地址留空");
			return {};
		}

		if (snapshot.rtsp_push_video_user_name.empty() || snapshot.rtsp_push_video_password.empty() ||
			snapshot.rtsp_push_video_base_url.empty() || snapshot.rtsp_push_video_server_port <= 0)
		{
			LOG_DEBUG("RTSP 推流配置不完整 (user/pass/port/base), 地址留空");
			return {};
		}

		return _FMT format(
			"rtsp://{}:{}@{}:{}/{}",
			snapshot.rtsp_push_video_user_name,
			snapshot.rtsp_push_video_password,
			*local_ip,
			snapshot.rtsp_push_video_server_port,
			snapshot.rtsp_push_video_base_url
		);
	}
} // namespace plane::utils

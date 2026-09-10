// cy_psdk/domain/AppConfigEntity.h

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "define.h"

namespace plane::domain
{
	struct AppConfigEntity
	{
		int			id { 1 };											// 主键 (单行固定 1)
		bool		virtual_stick_feature_enabled { true };				// 是否允许启用虚拟摇杆
		bool		force_enable_laser { false };						// 是否强制开启激光
		bool		use_mqtt_v5_server { true };						// 是否使用 MQTT v5
		bool		use_media3_player { true };							// 是否使用 Media3 (PSDK 预留)
		bool		enable_security_device_access { false };			// 是否启用安全设备访问
		float		waypoint_3d_distance_tolerance { 0.5f };			// 3D 航点距离容差 (m)
		float		stick_sensitivity { 0.33f };						// 摇杆灵敏度 0.1~1.0
		int			waypoint_geo_tolerance_exponent { 7 };				// 航点地理容差指数 1~10
		int			tcp_frame_server_port { 1234 };						// TCP 帧服务端口
		int			service_reconnect_interval_s { 2 };					// 服务重连间隔 (s)
		int			max_waypoints_per_mission { 500 };					// 单任务最大航点数
		int			max_total_waypoints { 1000 };						// 最大总航点数
		int			heartbeat_interval_s { 3 };							// 心跳间隔 (s)
		int			gps_satellite_alert_threshold { 15 };				// GPS 卫星告警阈值
		int			video_quality_level { 75 };							// 视频质量等级 10~100
		int64_t		last_run_time { 0 };								// 上次运行时间戳
		int64_t		authoritative_end_time { 0 };						// 权威结束时间戳
		_STD string local_uuid { "" };									// 本地默认 uuid
		_STD string simulator_default_longitude { "118.892591" };		// 模拟器默认经度
		_STD string simulator_default_latitude { "32.067228" };			// 模拟器默认纬度
		_STD string simulator_default_gps_count { "15" };				// 模拟器默认卫星数量
		_STD string last_used_plane_code { "" };						// 上次使用的飞机编号
		_STD string registry_ip { "127.0.0.1" };						// 默认注册服务地址
		_STD string catalog_node_id { "" };								// 当前选中的 Catalog nodeId
		_STD vector<_STD string> catalog_targets {};					// 当前选中节点的探测目标
		double					 altitude_difference_threshold { 2.0 }; // 高度差告警阈值 (m)
		double					 custom_central_meridian { 0.0 };		// 自定义中央子午线 (度)
	};
} // namespace plane::domain

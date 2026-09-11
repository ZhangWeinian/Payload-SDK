// cy_psdk/domain/PlaneStateDataClass.h

#pragma once

#include "domain/AppConfigEntity.h"
#include "domain/CatalogNode.h"
#include "domain/NetTestDataClass.h"
#include "domain/SwarmDataClass.h"

#include <cstdint>
#include <string>
#include <vector>

#include "define.h"

namespace plane::domain
{
	// 坐标/姿态/速度等基础量
	struct LocationCoordinate3D
	{
		double latitude { 0.0 };  // 纬度 (度)
		double longitude { 0.0 }; // 经度 (度)
		double altitude { 0.0 };  // 相对高度 (m)
	};

	struct DoublePoint2D
	{
		double x { 0.0 };
		double y { 0.0 };
	};

	struct DoubleMinMax
	{
		double min { 0.0 };
		double max { 0.0 };
	};

	struct Attitude
	{
		double pitch { 0.0 }; // 俯仰 (度)
		double roll { 0.0 };  // 横滚 (度)
		double yaw { 0.0 };	  // 偏航 (度)
	};

	struct Velocity3D
	{
		double x { 0.0 }; // x NED 速度 (m/s)
		double y { 0.0 }; // y NED 速度 (m/s)
		double z { 0.0 }; // z NED 速度 (m/s)
	};

	enum class LaserMeasureState
	{
		UNKNOWN = 0 /*其余见 SDK*/
	};
	enum class GPSSignalLevel
	{
		UNKNOWN = 0 /*其余: LEVEL_0..5 见 SDK*/
	};
	enum class NavigationSatelliteSystem
	{
		GPS_GLONASS = 0,
		BEIDOU,
		UNKNOWN
	};
	enum class ProductType
	{
		UNKNOWN = 0 /*其余见 SDK*/
	};
	enum class CameraType
	{
		NOT_SUPPORTED = 0 /*其余见 SDK*/
	};
	enum class ComponentIndexType
	{
		LEFT_OR_MAIN = 0 /*其余见 SDK*/
	};
	enum class FlightControlAuthority
	{
		UNKNOWN = 0, // 未知
		RC,			 // 遥控器
		MSDK,		 // 移动端 App
		PSDK,		 // 机载 PSDK 程序
		DOCK		 // 机场/机库
	};
	enum class RemoteControllerFlightMode
	{
		UNKNOWN = 0 /*其余: P/A/S 等见 SDK*/
	};
	enum class LandingProtectionState
	{
		UNKNOWN = 0 /*其余见 SDK*/
	};
	enum class LowBatteryRTHState
	{
		UNKNOWN = 0 /*其余见 SDK*/
	};
	enum class CameraMode
	{
		UNKNOWN = 0 /*其余见 SDK*/
	};

	enum class FlightMode
	{
		MANUAL = 0,
		ATTI,
		GPS_NORMAL,
		GPS_SPORT,
		GPS_TRIPOD,
		MOTOR_START,
		TAKE_OFF_READY,
		AUTO_TAKE_OFF,
		AUTO_LANDING,
		FORCE_LANDING,
		GO_HOME,
		WAYPOINT,
		VIRTUAL_STICK,
		SMART_FLY,
		POI,
		PANO,
		AUTO_AVOIDANCE,
		APAS,
		SMART_FLIGHT,
		ATTI_LANDING,
		CLICK_GO,
		CINEMATIC,
		DRAW,
		FOLLOW_ME,
		GPS_NOVICE,
		QUICK_MOVIE,
		TAP_FLY,
		MASTER_SHOT,
		TIME_LAPSE,
		UNKNOWN
	};

	enum class WaypointMissionExecuteState
	{
		UNKNOWN = 0,
		UPLOADING,
		PREPARING,
		ENTER_WAYLINE,
		EXECUTING,
		INTERRUPTED,
		RECOVERING,
		DISCONNECTED,
		IDLE,
		NOT_SUPPORTED,
		READY,
		FINISHED,
		RETURN_TO_START_POINT
	};

	enum class CameraVideoStreamSourceType
	{
		DEFAULT_CAMERA = 0,
		WIDE_CAMERA,
		ZOOM_CAMERA,
		INFRARED_CAMERA,
		NDVI_CAMERA,
		VISION_CAMERA,
		MS_G_CAMERA,
		MS_R_CAMERA,
		MS_RE_CAMERA,
		MS_NIR_CAMERA,
		POINT_CLOUD_CAMERA,
		RGB_CAMERA,
		UNKNOWN
	};

	// PlaneState.kt 内枚举 (数据类引用) 镜像
	enum class BackgroundLayer
	{
		MAP = 0,
		FPV,
		RTSP
	};
	enum class ZoneWidth
	{
		DEGREE_3 = 0,
		DEGREE_6
	};
	enum class CentralMeridianMode
	{
		TAKEOFF_RAW = 0,
		TAKEOFF_NORMALIZED,
		TARGET_RAW,
		TARGET_NORMALIZED
	};

	// 相机变焦范围
	struct ZoomRatiosRange
	{
		bool is_continuous { false }; // 是否连续变焦
		_STD vector<double> gears {}; // 关键档位
	};

	// 云台角度限位
	struct GimbalAttitudeRange
	{
		DoubleMinMax roll {};  // 云台横滚限位
		DoubleMinMax pitch {}; // 云台俯仰限位
		DoubleMinMax yaw {};   // 云台偏航限位
	};

	// 激光测距信息
	struct LaserMeasureInformation
	{
		LocationCoordinate3D location3d {};	   // 目标经纬高
		double				 distance { 0.0 }; // 目标直线距离
		DoublePoint2D		 target_point {};  // 目标点画面位置
		LaserMeasureState	 laser_measure_state { LaserMeasureState::UNKNOWN };
	};

	// 虚拟摇杆状态
	struct VirtualStickState
	{
		bool				   is_virtual_stick_enable { false };									 // 虚拟摇杆是否启用
		FlightControlAuthority current_control_permission_owner { FlightControlAuthority::UNKNOWN }; // 控制权归属
		bool				   is_virtual_stick_advanced_mode_enabled { false };					 // 高级模式是否启用
	};

	// 智能低电量返航评估
	struct LowBatteryRTHInfo
	{
		int				   battery_percent_needed_to_go_home { 0 }; // 需返航电量 (%)
		int				   battery_percent_needed_to_land { 0 };	// 需降落电量 (%)
		double			   max_radius_can_fly_and_go_home { 0.0 };	// 可安全返航最大半径 (m)
		int				   smart_rth_countdown { 0 };				// 返航确认倒计时 (s)
		int				   remaining_flight_time { 0 };				// 预估剩余飞行时间 (s)
		int				   time_needed_to_go_home { 0 };			// 返航所需时间 (s)
		int				   time_needed_to_land { 0 };				// 降落所需时间 (s)
		LowBatteryRTHState low_battery_rth_status { LowBatteryRTHState::UNKNOWN };
	};

	struct PlaneStateDataClass
	{
		// 飞行器核心状态
		bool				 running_simulator { false };			 // 模拟器运行状态
		LocationCoordinate3D plane_location_3d {};					 // 飞机当前经纬度 + 相对高度
		double				 abs_height { 0.0 };					 // 飞机海拔高度 (m)
		double				 aircraft_velocity_flight_speed { 0.0 }; // 地面速度 (m/s)
		int					 control_mode { 0 };					 // 控制模式: 0 手动; 1 程序
		int					 manually_stop_task { 0 };				 // 是否被手动停止航线任务: 0 否; 1 是

		// 激光目标
		LaserMeasureInformation laser_measure_information {};			 // 激光测距信息
		double					laser_measure_target_rel_height { 0.0 }; // 目标相对高度

		// 电池状态
		int aircraft_battery_voltage { 0 };				// 飞机电压 (mV)
		int aircraft_battery_power_percent { 0 };		// 飞机电量 (%)
		int remote_control_battery_power_percent { 0 }; // 遥控器电量 (%)

		// 速度与姿态
		Velocity3D aircraft_velocity_3d {}; // x/y/z NED 速度
		Attitude   aircraft_attitude {};	// NED 姿态

		// NED 飞机位置
		double aircraft_location_east { 0.0 };
		double aircraft_location_north { 0.0 };
		double aircraft_location_down { 0.0 };

		// GPS 状态
		int						  gps_satellite_count { 0 };										  // 卫星数量
		GPSSignalLevel			  gps_signal_level { GPSSignalLevel::UNKNOWN };						  // GPS 信号等级
		bool					  home_location_set { false };										  // 返航点是否已设置
		NavigationSatelliteSystem navigation_satellite_system { NavigationSatelliteSystem::UNKNOWN }; // 定位卫星系统

		// 产品信息
		ProductType product_type { ProductType::UNKNOWN };	   // 飞机类型
		CameraType	camera_type { CameraType::NOT_SUPPORTED }; // 相机类型
		bool		aircraft_connected { false };			   // 飞行器连接状态

		// 航线任务状态
		_STD string					kmz_file_path { "" };													 // 活动航线文件
		FlightMode					flight_mode { FlightMode::UNKNOWN };									 // 飞行模式
		WaypointMissionExecuteState waypoint_mission_execute_state { WaypointMissionExecuteState::UNKNOWN }; // 航点任务状态
		int							current_point { 0 };													 // 当前航点
		int							total_point { 0 };														 // 总航点
		bool						mission_pausing { false };												 // 航线任务是否已暂停

		// 云台状态
		Attitude		   gimbal_attitude {};											  // 云台角 (yaw 已做 IMU 补偿)
		double			   imu_coordinate_tran { 0.0 };									  // IMU 坐标系转换角度
		int				   camera_angle_mode { 0 };										  // 云台模式 (GimbalCMode 码)
		ComponentIndexType active_camera_index { ComponentIndexType::LEFT_OR_MAIN };	  // 当前活跃相机
		_STD vector<CameraVideoStreamSourceType> camera_video_stream_source_range {};	  // 视频流类型列表
		double									 real_camera_optical_zoom_factor { 1.0 }; // 光学变焦倍数
		double									 camera_optical_zoom_factor { 1.0 };	  // 兼容处理后的变焦倍数
		int										 camera_focal_length { 0 };				  // 等效焦距
		ZoomRatiosRange							 zoom_ratios_range {};					  // 摄像头变焦范围
		GimbalAttitudeRange						 gimbal_attitude_range {};				  // 云台限位
		_STD vector<CameraMode> camera_mode_range {};									  // 可设置的相机模式 (码)

		// 虚拟摇杆 / 遥控器
		VirtualStickState		   virtual_stick_state {};										// 虚拟摇杆状态
		RemoteControllerFlightMode remote_control_mode { RemoteControllerFlightMode::UNKNOWN }; // 遥控器 P/A/S 档

		// 返航点
		LocationCoordinate3D home_location {};				 // 返航点经纬度 (仅用经纬度)
		double				 home_location_altitude { 0.0 }; // 返航点海拔
		bool				 is_home_location_set { false }; // 返航点是否已设置
		int					 go_home_height { 0 };			 // 返航相对高度 (m)

		// 限高限远
		int	 height_limit { 120 };				// 限高 (m)
		int	 height_limit_range_min { 20 };		// 限高最小 (m)
		int	 height_limit_range_max { 500 };	// 限高最大 (m)
		bool is_near_height_limit { false };	// 是否已达限高
		bool distance_limit_enabled { false };	// 限远开关
		int	 distance_limit { 0 };				// 限远 (m)
		int	 distance_limit_range_min { 15 };	// 限远最小 (m)
		int	 distance_limit_range_max { 8000 }; // 限远最大 (m)
		bool is_near_distance_limit { false };	// 是否已达限远

		// 降落状态
		LandingProtectionState landing_protection_state { LandingProtectionState::UNKNOWN }; // 落地保护状态
		bool				   landing_confirmation_needed { false };						 // 是否需要落地确认

		// 飞行状态
		bool are_motors_on { false };  // 电机是否起转
		bool airlink_flying { false }; // 是否在空中

		// 飞行统计
		int	   flight_time_in_seconds { 0 };		   // 当次飞行时间 (s)
		double aircraft_total_flight_duration { 0.0 }; // 飞行总时长 (s)
		double aircraft_total_flight_distance { 0.0 }; // 飞行总距离 (m)
		int	   aircraft_total_flight_times { 0 };	   // 飞行总次数

		// 后台服务连接状态
		_STD string localhost_ip { "" };											   // 本地 IP
		_STD string swarm_agent_identifier { "" };									   // = "swarm.agent.<SN>"
		_STD string swarm_agent_name { "" };										   // = "swarm.agent.<SN>.server"
		_STD string device_nickname { "未绑定" };									   // 设备昵称
		_STD string internal_plane_id { "" };										   // 后台内部飞机 ID
		bool		network_service_connected { false };							   // 网络服务连接
		bool		device_binding { false };										   // 有效绑定
		bool		mqtt_connected { false };										   // MQTT 是否连接
		_STD string mqtt_connected_url { "" };										   // MQTT 地址
		bool		web_socket_connected { false };									   // WebSocket 是否连接
		_STD string web_socket_connected_url { "" };								   // WebSocket 地址
		bool		rtsp_push_video { false };										   // RTSP 推流状态
		int			rtsp_push_video_fps { 0 };										   // RTSP 帧率
		_STD string rtsp_push_video_user_name { "admin" };							   // RTSP 用户名
		_STD string rtsp_push_video_password { "1" };								   // RTSP 密码
		_STD string rtsp_push_video_base_url { "streaming/live/1" };				   // RTSP 后缀
		int			rtsp_push_video_server_port { 8554 };							   // RTSP 端口
		bool		tcp_push_data { false };										   // TCP 推流状态
		_STD string catalog_state { "" };											   // Catalog SDK 状态名
		bool		catalog_ready { false };										   // Catalog 是否就绪
		_STD string catalog_endpoint { "" };										   // Catalog "ip:httpPort"
		_STD string catalog_instance_id { "" };										   // Catalog 实例 ID
		_STD vector<CatalogNode> discovered_catalog_nodes {};						   // 自动发现节点
		_STD vector<CatalogNode> manual_catalog_nodes {};							   // 手动节点
		bool					 catalog_probe_listening { false };					   // 公告监听是否运行
		int						 catalog_probe_count { 0 };							   // 累计公告数
		_STD vector<PortHealthState> port_health {};								   // 端口层观测
		_STD string					 swarm_perception_rtsp_pull_video_head_url { "" }; // 目标流 rtsp 地址头
		_STD string					 swarm_perception_id { "" };					   // 目标感知 rtsp 查询 id
		bool						 swarm_plane_connect { false };					   // 后台机载是否连接本机

		// 图传状态
		int			airlink_signal_quality { 0 };			// 信号质量 0-100
		double		airlink_dynamic_data_rate { 0.0 };		// 图传码率 (Mbps)
		_STD string airlink_frequency_band { "" };			// 工作频段
		_STD string airlink_bandwidth { "" };				// 下行带宽
		_STD string airlink_air_link_type { "" };			// 图传类型
		_STD vector<_STD string> frequency_interference {}; // msdk: List<FrequencyInterferenceInfo>(SDK 结构) → 基础类型占位
		_STD vector<_STD string> frequency_band_range {};	// msdk: List<FrequencyBand>(SDK 枚举) → 基础类型占位

		// 电池详情 (主电池)
		double battery_temperature { 0.0 };						 // 温度 (℃)
		int	   battery_current { 0 };							 // 电流 (mA, 负=放电)
		int	   battery_number_of_discharges { 0 };				 // 总放电次数
		int	   battery_number_of_cells { 0 };					 // 电芯个数
		_STD vector<int> battery_cell_voltages {};				 // 各电芯电压 (mV)
		int64_t			 battery_high_voltage_storage_sec { 0 }; // 高电压存储时间 (s)

		// 低电量告警阈值
		int serious_low_battery_warning_threshold { 0 }; // 严重低电量阈值 (%)
		int low_battery_warning_threshold { 0 };		 // 低电量阈值 (%)

		// 智能低电量返航评估
		LowBatteryRTHInfo low_battery_rth_info {};

		// WebSocket Target Info (感知/后端链路, PSDK 预留)
		bool		tracking_state_enabled { false };
		bool		is_tracking_correcting { false };
		_STD string selected_target_type { "" };
		_STD string selected_target_type_name { "" };
		double		track_lat { 0.0 };
		double		track_lon { 0.0 };
		double		track_gauss_east { 0.0 };
		double		track_gauss_north { 0.0 };
		double		track_abs_height { 0.0 };
		int			maximum_speed_during_tracking { 0 };

		// pip 状态相关 (UI 决策)
		BackgroundLayer				background_layer { BackgroundLayer::MAP };
		CameraVideoStreamSourceType camera_video_stream_source { CameraVideoStreamSourceType::WIDE_CAMERA };

		// 固定设备信息 (App 生命周期内不变, 连接后刷新)
		_STD string app_version { "" };				 // 应用/代理版本
		_STD string sdk_version { "" };				 // SDK 版本 (psdk: PSDK 版本)
		_STD string sdk_build_version { "" };		 // SDK 构建号
		bool		is_debug_sdk_build { false };	 // 是否 Debug
		bool		is_us_version { false };		 // 是否美版
		_STD string package_category { "" };		 // 产品类别
		_STD string core_info { "" };				 // 内核信息
		_STD string product_firmware_version { "" }; // 飞机固件版本
		_STD string rc_firmware_version { "" };		 // 遥控器固件版本
		_STD string camera_firmware_version { "" };	 // 相机固件版本
		_STD string serial_number { "" };			 // 飞控序列号

		// 高斯坐标转换设置
		CentralMeridianMode coordinate_transformation_mode { CentralMeridianMode::TARGET_NORMALIZED };
		ZoneWidth			coordinate_transformation_zone_width { ZoneWidth::DEGREE_6 };

		// 网络状态
		PingResult ping_swarm_server_side {};			   // 注册服务地址延迟
		_STD vector<PingResult> ping_tcp_clients {};	   // TCP 客户端延迟栈
		_STD vector<SwarmSubscriber> swarm_subscribers {}; // Swarm 订阅者

		// 应用运行时配置
		AppConfigEntity app_config {};

		// 杂项
		bool		central_meridian_manually_set { false }; // 是否手动指定中央子午线
		bool		registry_ip_manually_set { false };		 // 注册中心 IP 是否手动输入
		_STD string udp_multicast_group { "239.255.18.18" }; // UDP 组播默认地址
		int			udp_multicast_port { 38'500 };			 // UDP 组播默认端口
		int			udp_receive_buffer { 16'384 };			 // UDP 组播默认接收缓冲
	};
} // namespace plane::domain

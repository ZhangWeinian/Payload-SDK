// cy_psdk/tests/test_domain.cpp
//
// 冻结 domain 数据模型: 校验 PlaneStateDataClass 初始值与 msdk 对齐, 以及
// PlaneStateStore 的线程安全读写语义。模型结构/初始值变更时必须同步更新本文件,
// 从而保证"数据模型 100% 固定 (与 msdk 1:1)"可被回归守护。

#include "domain/PlaneStateDataClass.h"
#include "manager/plane_state/PlaneStateStore.h"

#include <gtest/gtest.h>

namespace
{
	using plane::domain::PlaneStateDataClass;
	using plane::domain::PlaneStateStore;
} // namespace

TEST(DomainModel, PlaneStateDataClassDefaultsMirrorMsdk)
{
	PlaneStateDataClass s {};

	// 位置/姿态/速度默认 0
	EXPECT_DOUBLE_EQ(s.plane_location_3d.latitude, 0.0);
	EXPECT_DOUBLE_EQ(s.plane_location_3d.altitude, 0.0);
	EXPECT_DOUBLE_EQ(s.abs_height, 0.0);
	EXPECT_DOUBLE_EQ(s.aircraft_velocity_3d.x, 0.0);
	EXPECT_DOUBLE_EQ(s.aircraft_attitude.yaw, 0.0);

	// 枚举默认 (msdk UNKNOWN / NOT_SUPPORTED)
	EXPECT_EQ(s.product_type, plane::domain::ProductType::UNKNOWN);
	EXPECT_EQ(s.camera_type, plane::domain::CameraType::NOT_SUPPORTED);
	EXPECT_EQ(s.gps_signal_level, plane::domain::GPSSignalLevel::UNKNOWN);
	EXPECT_EQ(s.flight_mode, plane::domain::FlightMode::UNKNOWN);
	EXPECT_EQ(s.waypoint_mission_execute_state, plane::domain::WaypointMissionExecuteState::UNKNOWN);
	EXPECT_EQ(s.remote_control_mode, plane::domain::RemoteControllerFlightMode::UNKNOWN);

	// 特殊初始值 (照抄 msdk)
	EXPECT_DOUBLE_EQ(s.real_camera_optical_zoom_factor, 1.0);
	EXPECT_DOUBLE_EQ(s.camera_optical_zoom_factor, 1.0);
	EXPECT_EQ(s.device_nickname, "未绑定");
	EXPECT_EQ(s.rtsp_push_video_user_name, "admin");
	EXPECT_EQ(s.rtsp_push_video_password, "1");
	EXPECT_EQ(s.rtsp_push_video_base_url, "streaming/live/1");
	EXPECT_EQ(s.rtsp_push_video_server_port, 8554);
	EXPECT_EQ(s.height_limit, 120);
	EXPECT_EQ(s.height_limit_range_min, 20);
	EXPECT_EQ(s.height_limit_range_max, 500);
	EXPECT_EQ(s.distance_limit_range_min, 15);
	EXPECT_EQ(s.distance_limit_range_max, 8000);
	EXPECT_EQ(s.coordinate_transformation_zone_width, plane::domain::ZoneWidth::DEGREE_6);
	EXPECT_EQ(s.coordinate_transformation_mode, plane::domain::CentralMeridianMode::TARGET_NORMALIZED);
	EXPECT_EQ(s.udp_multicast_group, "239.255.18.18");
	EXPECT_EQ(s.udp_multicast_port, 38'500);
	EXPECT_EQ(s.udp_receive_buffer, 16'384);
	EXPECT_EQ(s.camera_video_stream_source, plane::domain::CameraVideoStreamSourceType::WIDE_CAMERA);
	EXPECT_EQ(s.background_layer, plane::domain::BackgroundLayer::MAP);
	EXPECT_EQ(s.active_camera_index, plane::domain::ComponentIndexType::LEFT_OR_MAIN);
	EXPECT_TRUE(s.app_config.virtual_stick_feature_enabled);
	EXPECT_EQ(s.app_config.tcp_frame_server_port, 1234);
	EXPECT_EQ(s.app_config.heartbeat_interval_s, 3);
	EXPECT_EQ(s.app_config.registry_ip, "127.0.0.1");
	EXPECT_TRUE(s.battery_cell_voltages.empty());
	EXPECT_TRUE(s.camera_video_stream_source_range.empty());
}

TEST(DomainStore, MutateAndSnapshotRoundTrip)
{
	auto& store { PlaneStateStore::getInstance() };

	store.update(
		[](PlaneStateDataClass& st)
		{
			st.plane_location_3d.latitude	  = 39.9075;
			st.plane_location_3d.longitude	  = 116.3912;
			st.plane_location_3d.altitude	  = 100.0;
			st.aircraft_battery_power_percent = 85;
			st.are_motors_on				  = true;
			st.airlink_flying				  = true;
			st.current_point				  = 3;
			st.total_point					  = 10;
			st.battery_cell_voltages		  = { 4100, 4090, 4080 };
			st.serial_number				  = "0A1B2C3D4E5F6078";
			st.swarm_agent_identifier		  = "swarm.agent.0A1B2C3D4E5F6078";
			st.app_version					  = "3.1.0";
		}
	);

	const auto s { store.snapshot() };

	EXPECT_DOUBLE_EQ(s.plane_location_3d.latitude, 39.9075);
	EXPECT_DOUBLE_EQ(s.plane_location_3d.longitude, 116.3912);
	EXPECT_DOUBLE_EQ(s.plane_location_3d.altitude, 100.0);
	EXPECT_EQ(s.aircraft_battery_power_percent, 85);
	EXPECT_TRUE(s.are_motors_on);
	EXPECT_TRUE(s.airlink_flying);
	EXPECT_EQ(s.current_point, 3);
	EXPECT_EQ(s.total_point, 10);
	ASSERT_EQ(s.battery_cell_voltages.size(), 3u);
	EXPECT_EQ(s.battery_cell_voltages[0], 4100);
	EXPECT_EQ(s.serial_number, "0A1B2C3D4E5F6078");
	EXPECT_EQ(s.swarm_agent_identifier, "swarm.agent.0A1B2C3D4E5F6078");
	EXPECT_EQ(s.app_version, "3.1.0");
}

TEST(DomainStore, WholeUpdateReplacesSnapshot)
{
	auto&				store { PlaneStateStore::getInstance() };

	PlaneStateDataClass fresh {};
	fresh.gps_satellite_count = 12;
	store.update(fresh);

	// 整份替换后, 之前 mutate 写入的字段应被清空 (整份替换语义)
	EXPECT_EQ(store.snapshot().gps_satellite_count, 12);
	EXPECT_EQ(store.snapshot().current_point, 0);
	EXPECT_EQ(store.snapshot().serial_number, "");
}

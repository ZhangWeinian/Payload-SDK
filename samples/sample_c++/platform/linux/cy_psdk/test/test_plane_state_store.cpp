// cy_psdk/test/test_plane_state_store.cpp
//
// PlaneStateStore 单测: 验证"整份替换 vs mutator 就地更新"的核心语义——
// 各模块经 mutator 只更新自己的字段, 不会被其他模块的写入清零
// (PSDK 采集循环 20ms 级整份覆盖曾导致 SN/绑定/MQTT/catalog 状态被清空, 此测试防止回归)。

#include <gtest/gtest.h>

#include <string>

#include "define.h"
#include "manager/plane_state/PlaneStateStore.h"

namespace
{
	using plane::domain::PlaneStateDataClass;
	using plane::domain::PlaneStateStore;

	TEST(PlaneStateStoreTest, MutatorUpdatePreservesOtherFields)
	{
		auto& store { PlaneStateStore::getInstance() };

		// 模块 A (绑定): 写入绑定信息
		store.update(
			[](PlaneStateDataClass& st)
			{
				st.device_nickname	 = "DJIM4T-003";
				st.internal_plane_id = "abc123";
				st.device_binding	 = true;
			}
		);

		// 模块 B (PSDK 采集): 只更新自己负责的字段
		store.update(
			[](PlaneStateDataClass& st)
			{
				st.aircraft_battery_power_percent = 87;
				st.gps_satellite_count			  = 15;
			}
		);

		const auto snapshot { store.snapshot() };
		EXPECT_EQ(snapshot.device_nickname, "DJIM4T-003");
		EXPECT_TRUE(snapshot.device_binding);
		EXPECT_EQ(snapshot.aircraft_battery_power_percent, 87);
		EXPECT_EQ(snapshot.gps_satellite_count, 15);
	}

	TEST(PlaneStateStoreTest, SnapshotIsIndependentCopy)
	{
		auto& store { PlaneStateStore::getInstance() };
		store.update(
			[](PlaneStateDataClass& st)
			{
				st.mqtt_connected	  = true;
				st.mqtt_connected_url = "tcp://127.0.0.1:1883";
			}
		);

		auto copy { store.snapshot() };
		copy.mqtt_connected = false; // 修改副本不得影响存储

		const auto latest { store.snapshot() };
		EXPECT_TRUE(latest.mqtt_connected);
		EXPECT_EQ(latest.mqtt_connected_url, "tcp://127.0.0.1:1883");
	}
} // namespace

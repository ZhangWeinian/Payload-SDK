// cy_psdk/test/test_plane_state_store.cpp
//
// PlaneStateStore 单测: 验证运行时状态存储的核心语义——
//   1) "整份替换 vs mutator 就地更新": 各模块经 mutator 只更新自己的字段, 不会被其他模块的写入清零
//      (PSDK 采集循环 20ms 级整份覆盖曾导致 SN/绑定/MQTT/catalog 状态被清空, 此测试防止回归);
//   2) 字段级 set/get/modify/read: 单字段读写不整份复制, 多字段读取同一把锁内取齐;
//   3) 跨线程安全: 多写多读线程并发压力下无丢失更新、无"半新半旧"组合 (配合 tsan preset 验证)。

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <string>
#include <thread>
#include <vector>

#include "define.h"
#include "manager/plane_state/PlaneStateStore.h"

namespace
{
    using plane::domain::FlightMode;
    using plane::domain::PlaneStateDataClass;
    using plane::domain::PlaneStateStore;

    TEST(PlaneStateStoreTest, MutatorUpdatePreservesOtherFields)
    {
        auto& store { PlaneStateStore::getInstance() };

        // 模块 A (绑定): 写入绑定信息
        store.update(
            [](PlaneStateDataClass& st)
            {
                st.device_nickname   = "DJIM4T-003";
                st.internal_plane_id = "abc123";
                st.device_binding    = true;
            }
        );

        // 模块 B (PSDK 采集): 只更新自己负责的字段
        store.update(
            [](PlaneStateDataClass& st)
            {
                st.aircraft_battery_power_percent = 87;
                st.gps_satellite_count            = 15;
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
                st.mqtt_connected     = true;
                st.mqtt_connected_url = "tcp://127.0.0.1:1883";
            }
        );

        auto copy { store.snapshot() };
        copy.mqtt_connected = false; // 修改副本不得影响存储

        const auto latest { store.snapshot() };
        EXPECT_TRUE(latest.mqtt_connected);
        EXPECT_EQ(latest.mqtt_connected_url, "tcp://127.0.0.1:1883");
    }

    TEST(PlaneStateStoreTest, FieldLevelSetGetModify)
    {
        auto& store { PlaneStateStore::getInstance() };

        // 单字段写入/读取 (double / string / enum): 只复制字段本身, 不整份快照
        store.set(&PlaneStateDataClass::aircraft_location_east, 123.5);
        EXPECT_DOUBLE_EQ(store.get(&PlaneStateDataClass::aircraft_location_east), 123.5);

        store.set(&PlaneStateDataClass::selected_target_type, ::std::string { "car" });
        EXPECT_EQ(store.get(&PlaneStateDataClass::selected_target_type), "car");

        store.set(&PlaneStateDataClass::flight_mode, FlightMode::WAYPOINT);
        EXPECT_EQ(store.get(&PlaneStateDataClass::flight_mode), FlightMode::WAYPOINT);

        // 单字段就地读改写 (计数器自增语义)
        store.set(&PlaneStateDataClass::catalog_probe_count, 41);
        store.modify(
            &PlaneStateDataClass::catalog_probe_count,
            [](int& count)
            {
                ++count;
            }
        );
        EXPECT_EQ(store.get(&PlaneStateDataClass::catalog_probe_count), 42);

        // 字段级操作只触碰目标字段, 不影响其它字段
        EXPECT_EQ(store.get(&PlaneStateDataClass::selected_target_type), "car");
        EXPECT_EQ(store.get(&PlaneStateDataClass::flight_mode), FlightMode::WAYPOINT);
    }

    TEST(PlaneStateStoreTest, MultiFieldReadIsCoherent)
    {
        auto& store { PlaneStateStore::getInstance() };
        store.update(
            [](PlaneStateDataClass& st)
            {
                st.aircraft_location_east  = 1.25;
                st.aircraft_location_north = -3.5;
                st.aircraft_location_down  = 6.0;
            }
        );

        // 同一把锁内一次取齐多个字段 (不整份快照)
        const auto [east, north, down] { store.read(
            &PlaneStateDataClass::aircraft_location_east,
            &PlaneStateDataClass::aircraft_location_north,
            &PlaneStateDataClass::aircraft_location_down
        ) };
        EXPECT_DOUBLE_EQ(east, 1.25);
        EXPECT_DOUBLE_EQ(north, -3.5);
        EXPECT_DOUBLE_EQ(down, 6.0);
    }

    TEST(PlaneStateStoreTest, ConcurrentFieldAccessKeepsCoherenceAndUpdates)
    {
        auto&         store { PlaneStateStore::getInstance() };

        constexpr int kWriterCount { 4 };
        constexpr int kWriterIterations { 10'000 };
        constexpr int kReaderMaxIterations { 50'000 };
        constexpr int kReaderCount { 2 };

        // 基线: 计数器与成对字段置确定初值 (更早的测试可能写过这些字段)
        store.set(&PlaneStateDataClass::catalog_probe_count, 0);
        store.update(
            [](PlaneStateDataClass& st)
            {
                st.aircraft_location_east  = 0.0;
                st.aircraft_location_north = 0.0;
            }
        );

        ::std::atomic<bool> stopping { false };
        ::std::atomic<int>  pair_violations { 0 };

        // 写线程: 高频 set (单字段) + modify (共享计数, 检验无丢失更新) + update (成对字段一致写)
        ::std::vector<::std::thread> threads;
        threads.reserve(static_cast<::std::size_t>(kWriterCount + kReaderCount));
        for (int w = 0; w < kWriterCount; ++w)
        {
            threads.emplace_back(
                [&store, w]
                {
                    for (int n = 1; n <= kWriterIterations; ++n)
                    {
                        store.set(&PlaneStateDataClass::aircraft_velocity_flight_speed, static_cast<double>(n));
                        store.modify(
                            &PlaneStateDataClass::catalog_probe_count,
                            [](int& count)
                            {
                                ++count;
                            }
                        );
                        store.update(
                            [value = static_cast<double>(w)](PlaneStateDataClass& st)
                            {
                                st.aircraft_location_east  = value;
                                st.aircraft_location_north = value;
                            }
                        );
                    }
                }
            );
        }

        // 读线程: 高频 get / read; 成对字段由写线程持同一把锁成对写入, 任何时刻读出的组合必须相等
        for (int r = 0; r < kReaderCount; ++r)
        {
            threads.emplace_back(
                [&store, &stopping, &pair_violations]
                {
                    for (int n = 0; n < kReaderMaxIterations && !stopping.load(::std::memory_order_relaxed); ++n)
                    {
                        const auto [east, north] {
                            store.read(&PlaneStateDataClass::aircraft_location_east, &PlaneStateDataClass::aircraft_location_north)
                        };
                        if (east != north)
                        {
                            pair_violations.fetch_add(1, ::std::memory_order_relaxed);
                        }
                        (void)store.get(&PlaneStateDataClass::aircraft_velocity_flight_speed);
                    }
                }
            );
        }

        // 等写线程结束再放行读线程 (vector 前 kWriterCount 个入口为写线程)
        for (int i = 0; i < kWriterCount; ++i)
        {
            threads[static_cast<::std::size_t>(i)].join();
        }
        stopping.store(true);
        for (::std::size_t i = kWriterCount; i < threads.size(); ++i)
        {
            threads[i].join();
        }

        // 无丢失更新: 4 个写线程各自增 kWriterIterations 次, 计数必须精确
        EXPECT_EQ(store.get(&PlaneStateDataClass::catalog_probe_count), kWriterCount * kWriterIterations);
        // 成对字段不存在"半新半旧"的读取组合
        EXPECT_EQ(pair_violations.load(), 0);
        // 单字段写入终值 = 某个写线程的最后一次写入 (落在 1..kWriterIterations 内)
        const auto speed { store.get(&PlaneStateDataClass::aircraft_velocity_flight_speed) };
        EXPECT_GE(speed, 1.0);
        EXPECT_LE(speed, static_cast<double>(kWriterIterations));
    }
} // namespace

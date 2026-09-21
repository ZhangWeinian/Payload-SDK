// cy_psdk/test/test_kmz.cpp
//
// KMZ / WPML 结构基线测试。
//
// 基线: 真机实测任务產物 test/sample_data/kmz_template.kmz (2026-09-15, 飞机正常执行),
// 与 kmz_template.json 逐项对照的严格等价比对见 test_kmz_reference.cpp;
// 本文件用 3 竹点的合成航点集快速校验同一套结构约定:
//   - 归档内只含 wpmz/template.kml 与 wpmz/waylines.wpml (无 res/)
//   - 命名空间为 http://www.dji.com/wpmz/1.0.6
//   - 不输出 takeOffSecurityHeight / payloadInfo / payloadParam; droneEnumValue = 65535 (未知)
//   - 航点 0: reachPoint + gimbalRotate (完整参数集)
//   - 航点 i (< 末点): betweenAdjacentPoints + gimbalEvenlyRotate, 角度取【下一航点】的 YTFYJ; 末点无该组
//   - template.kml 的 Placemark 逐点给 waypointSpeed / waypointHeadingParam / waypointTurnParam
//     (不用 useGlobal* 标记), height 固定 0, 高度由 ellipsoidHeight 携带
//   - 动作中的负载挂载位置 (payloadPositionIndex) 一律为 0
//   - 不再出现 takeoff 触发器与 gimbalAngleLock/startTimeLapse/stopTimeLapse/gimbalAngleUnlock
//
// 注: convertWaypointsToKmz() 若配置允许还会顺带把 KMZ 落盘 (与返回值无关);
//     该开关的真实生效性由 SaveDisabledWritesNoFile 覆盖。

#include "utils/EXEHomePath.h"
#include "utils/json_converter/JsonToKmz.h"

#include "config/ConfigManager.h"
#include "test_config_helpers.h"
#include "test_zip_helpers.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
    ::std::size_t countOccurrences(const ::std::string& haystack, const ::std::string& needle)
    {
        if (needle.empty())
        {
            return 0;
        }

        ::std::size_t count { 0 };
        for (::std::size_t pos { haystack.find(needle) }; pos != ::std::string::npos; pos = haystack.find(needle, pos + needle.size()))
        {
            ++count;
        }
        return count;
    }

    ::std::string readZipEntry(const ::std::vector<::std::uint8_t>& kmz, const ::std::string& entryName)
    {
        return ::plane::test::readZipEntry(kmz, entryName);
    }

    // KMZ 落盘目录 (与 JsonToKmz 内部一致: 可执行文件同级目录下的 kmz/) 中的 .kmz 文件数
    ::std::size_t countKmzFiles(const ::std::filesystem::path& dir)
    {
        if (!::std::filesystem::exists(dir))
        {
            return 0;
        }

        ::std::size_t count { 0 };
        for (const auto& entry : ::std::filesystem::directory_iterator(dir))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".kmz")
            {
                ++count;
            }
        }
        return count;
    }

    // 三个航点, 云台俯仰角各不相同, 用于验证"动作角度取自各航点 YTFYJ"
    ::std::vector<plane::protocol::Waypoint> buildTestWaypoints(void)
    {
        ::std::vector<plane::protocol::Waypoint> waypoints(3);

        waypoints[0].JD    = 118.927731744947;
        waypoints[0].WD    = 32.0109044978747;
        waypoints[0].GD    = 45.0;
        waypoints[0].SD    = 5.0;
        waypoints[0].YTFYJ = -90.0;

        waypoints[1].JD    = 118.928000000000;
        waypoints[1].WD    = 32.0110000000000;
        waypoints[1].GD    = 45.0;
        waypoints[1].SD    = 5.0;
        waypoints[1].YTFYJ = -70.0;

        waypoints[2].JD    = 118.928126000000;
        waypoints[2].WD    = 32.0108220000000;
        waypoints[2].GD    = 45.0;
        waypoints[2].SD    = 5.0;
        waypoints[2].YTFYJ = -60.0;

        return waypoints;
    }
} // namespace

class KmzGenerationTest: public ::testing::Test
{
protected:
    // 单独运行本文件时 (ConfigManager 用例未先行) 自行准备共享配置;
    // 全套运行时配置已加载, 重复 loadAndCheck 无副作用
    static void SetUpTestSuite()
    {
        if (plane::test::writeSharedConfig())
        {
            ::plane::config::ConfigManager::getInstance().loadAndCheck(plane::test::sharedConfigPath());
        }
    }

    void SetUp() override
    {
        const auto kmz { plane::utils::JsonToKmzConverter::convertWaypointsToKmz(buildTestWaypoints()) };
        ASSERT_TRUE(kmz.has_value());
        this->kmz_ = *kmz;
        ASSERT_FALSE(this->kmz_.empty());

        this->waylines_ = readZipEntry(this->kmz_, "wpmz/waylines.wpml");
        this->template_ = readZipEntry(this->kmz_, "wpmz/template.kml");
    }

    ::std::vector<::std::uint8_t> kmz_ {};
    ::std::string                 waylines_ {};
    ::std::string                 template_ {};
};

// 归档结构: wpmz/ 前缀 + 两个固定文件名, 与真机产物一致
TEST_F(KmzGenerationTest, ArchiveLayout)
{
    ::zip_error_t error {};
    ::zip_error_init(&error);
    ::zip_source_t* source { ::zip_source_buffer_create(this->kmz_.data(), this->kmz_.size(), 0, &error) };
    ASSERT_NE(source, nullptr);
    ::zip_t* archive { ::zip_open_from_source(source, ZIP_RDONLY, &error) };
    ASSERT_NE(archive, nullptr);

    ASSERT_EQ(2, ::zip_get_num_entries(archive, 0));
    EXPECT_GE(::zip_name_locate(archive, "wpmz/waylines.wpml", 0), 0);
    EXPECT_GE(::zip_name_locate(archive, "wpmz/template.kml", 0), 0);

    ::zip_close(archive);
    ::zip_error_fini(&error);
}

// 命名空间版本与真机可飞产物一致 (不是官方 PSDK 旧样例的 1.0.3)
TEST_F(KmzGenerationTest, NamespaceVersion)
{
    EXPECT_NE(::std::string::npos, this->waylines_.find("xmlns:wpml=\"http://www.dji.com/wpmz/1.0.6\""));
    EXPECT_NE(::std::string::npos, this->template_.find("xmlns:wpml=\"http://www.dji.com/wpmz/1.0.6\""));
}

// 任务级字段: 参考产物不输出 takeOffSecurityHeight / payloadInfo / payloadParam, 机型枚举用 65535
TEST_F(KmzGenerationTest, MissionConfigMatchesBaseline)
{
    EXPECT_EQ(::std::string::npos, this->waylines_.find("takeOffSecurityHeight"));
    EXPECT_EQ(::std::string::npos, this->waylines_.find("payloadInfo"));
    EXPECT_NE(::std::string::npos, this->waylines_.find("<wpml:droneEnumValue>65535</wpml:droneEnumValue>"));

    EXPECT_EQ(::std::string::npos, this->template_.find("takeOffSecurityHeight"));
    EXPECT_EQ(::std::string::npos, this->template_.find("payloadInfo"));
    EXPECT_EQ(::std::string::npos, this->template_.find("payloadParam")); // 参考产物的 template Folder 不带它
    EXPECT_NE(::std::string::npos, this->template_.find("<wpml:createTime>"));
    EXPECT_NE(::std::string::npos, this->template_.find("<wpml:updateTime>"));

    // 真机参考产物的 template.kml 逐点给显式参数, 不用 useGlobal* 这组标记
    for (const char* element : { "useGlobalHeight", "useGlobalSpeed", "useGlobalHeadingParam", "useGlobalTurnParam" })
    {
        EXPECT_EQ(::std::string::npos, this->template_.find(element)) << element;
    }
    EXPECT_EQ(::std::string::npos, this->template_.find("<wpml:height>50")) << "height 固定 0, 高度在 ellipsoidHeight";
}

// 动作组: 航点 0 = reachPoint + gimbalRotate; 航点 i (非末点) = betweenAdjacentPoints + gimbalEvenlyRotate
TEST_F(KmzGenerationTest, ActionGroupsMatchBaseline)
{
    EXPECT_EQ(3u, countOccurrences(this->waylines_, "<wpml:actionGroup>")); // 1 个 reachPoint + 2 个航段 (末点无)
    EXPECT_EQ(1u, countOccurrences(this->waylines_, "<wpml:actionTriggerType>reachPoint</wpml:actionTriggerType>"));
    EXPECT_EQ(2u, countOccurrences(this->waylines_, "<wpml:actionTriggerType>betweenAdjacentPoints</wpml:actionTriggerType>"));
    EXPECT_EQ(1u, countOccurrences(this->waylines_, "<wpml:actionActuatorFunc>gimbalRotate</wpml:actionActuatorFunc>"));
    EXPECT_EQ(2u, countOccurrences(this->waylines_, "<wpml:actionActuatorFunc>gimbalEvenlyRotate</wpml:actionActuatorFunc>"));

    // 非法/未使用的旧触发器与动作名不得再出现
    EXPECT_EQ(::std::string::npos, this->waylines_.find(">takeoff<"));
    EXPECT_EQ(::std::string::npos, this->waylines_.find("gimbalAngleLock"));
    EXPECT_EQ(::std::string::npos, this->waylines_.find("startTimeLapse"));
    EXPECT_EQ(::std::string::npos, this->waylines_.find("stopTimeLapse"));
    EXPECT_EQ(::std::string::npos, this->waylines_.find("gimbalAngleUnlock"));
}

// 云台俯仰角: reachPoint 组用首点, 每个航段的偶数旋转组用【下一航点】的 YTFYJ; 负载挂载位置一律 0
TEST_F(KmzGenerationTest, GimbalAnglesAndPayloadPosition)
{
    // 夹具 YTFYJ = -90 / -70 / -60: 首点组 -90, 段 0→1 取 -70, 段 1→2 取 -60
    EXPECT_EQ(1u, countOccurrences(this->waylines_, "<wpml:gimbalPitchRotateAngle>-90</wpml:gimbalPitchRotateAngle>"));
    EXPECT_EQ(1u, countOccurrences(this->waylines_, "<wpml:gimbalPitchRotateAngle>-70</wpml:gimbalPitchRotateAngle>"));
    EXPECT_EQ(1u, countOccurrences(this->waylines_, "<wpml:gimbalPitchRotateAngle>-60</wpml:gimbalPitchRotateAngle>"));

    EXPECT_EQ(3u, countOccurrences(this->waylines_, "<wpml:payloadPositionIndex>0</wpml:payloadPositionIndex>"));
    EXPECT_EQ(::std::string::npos, this->waylines_.find("<wpml:payloadPositionIndex>7</wpml:payloadPositionIndex>"));

    // gimbalEvenlyRotate 使用精简参数集 (仅 2 字段): 完整集字段只应出现在唯一的 gimbalRotate 里
    EXPECT_EQ(1u, countOccurrences(this->waylines_, "<wpml:gimbalRotateMode>"));
    EXPECT_EQ(1u, countOccurrences(this->waylines_, "<wpml:gimbalRotateTime>"));
    EXPECT_EQ(1u, countOccurrences(this->waylines_, "<wpml:gimbalHeadingYawBase>"));

    // 航点云台朝向不参与控制 (真机产物为 0), 控制改由动作组承担
    EXPECT_EQ(::std::string::npos, this->waylines_.find("<wpml:waypointGimbalPitchAngle>-90</wpml:waypointGimbalPitchAngle>"));
}

// 落盘开关必须真实生效: 夹具配置 features.save_kmz_file=false,
// 此时仍要返回可用的内存 KMZ, 但不得在落盘目录留下任何文件
TEST_F(KmzGenerationTest, SaveDisabledWritesNoFile)
{
    ASSERT_FALSE(::plane::config::ConfigManager::getInstance().isSaveKmz());

    const ::std::filesystem::path kmz_dir { ::plane::utils::getEXEHomePath("kmz") };
    const ::std::size_t           before { countKmzFiles(kmz_dir) };

    EXPECT_TRUE(::plane::utils::JsonToKmzConverter::convertWaypointsToKmz(buildTestWaypoints()).has_value());

    EXPECT_EQ(before, countKmzFiles(kmz_dir));
}

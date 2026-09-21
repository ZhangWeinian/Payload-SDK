// cy_psdk/utils/json_converter/JsonToKmz.cpp

#include "utils/json_converter/JsonToKmz.h"

#include "config/ConfigManager.h"
#include "protocol/KmzDataClass.h"
#include "utils/EXEHomePath.h"
#include "utils/log_util/Logger.h"
#include "utils/XmlUtils.h"

#include <fmt/format.h>
#include <gsl/gsl>
#include <zip.h>

#include <sys/stat.h>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <pwd.h>
#include <sstream>
#include <unistd.h>

namespace plane::utils
{
    namespace
    {
        static ::std::filesystem::path g_latestKmzFilePath {};
        static ::std::mutex            g_kmzPathMutex {};

        class InMemoryZipArchive
        {
        public:
            explicit InMemoryZipArchive(void) noexcept
            {
                ::zip_error_t error {};
                ::zip_error_init(&error);

                this->source_ = ::zip_source_buffer_create(nullptr, 0, 1, &error);
                if (!this->source_)
                {
                    LOG_ERROR("创建 zip 内存源失败: {}", ::zip_error_strerror(&error));
                    ::zip_error_fini(&error);
                    return;
                }

                ::zip_source_keep(this->source_);

                this->archive_ = ::zip_open_from_source(this->source_, ZIP_CREATE | ZIP_TRUNCATE, &error);
                if (!this->archive_)
                {
                    LOG_ERROR("从内存源打开 zip 归档失败: {}", ::zip_error_strerror(&error));
                    ::zip_source_free(this->source_);
                    this->source_ = nullptr;
                }
                ::zip_error_fini(&error);
            }

            ~InMemoryZipArchive(void) noexcept
            {
                if (this->archive_)
                {
                    ::zip_discard(this->archive_);
                    this->archive_ = nullptr;
                }
                if (this->source_)
                {
                    ::zip_source_free(this->source_);
                    this->source_ = nullptr;
                }
            }

            bool addFile(const ::std::string& pathInZip, const ::std::string& content)
            {
                if (!this->archive_)
                {
                    return false;
                }

                ::zip_source_t* content_source { ::zip_source_buffer(this->archive_, content.c_str(), content.length(), 0) };
                if (!content_source)
                {
                    LOG_ERROR("无法为 '{}' 创建 zip source: {}", pathInZip, ::zip_strerror(this->archive_));
                    return false;
                }

                if (::zip_file_add(this->archive_, pathInZip.c_str(), content_source, ZIP_FL_ENC_UTF_8 | ZIP_FL_OVERWRITE) < 0)
                {
                    LOG_ERROR("无法将 '{}' 添加到 KMZ: {}", pathInZip, ::zip_strerror(this->archive_));
                    ::zip_source_free(content_source);
                    return false;
                }
                return true;
            }

            ::std::optional<kmz_data_type> getFinalData(void)
            {
                if (!this->source_ || !this->archive_)
                {
                    return ::std::nullopt;
                }

                if (::zip_close(this->archive_) < 0)
                {
                    LOG_ERROR("关闭内存归档时出错: {}", ::zip_error_strerror(::zip_get_error(this->archive_)));
                    ::zip_discard(this->archive_);
                    this->archive_ = nullptr;
                    return ::std::nullopt;
                }
                this->archive_         = nullptr;

                auto source_free_guard = ::gsl::finally(
                    [this]
                    {
                        if (this->source_)
                        {
                            ::zip_source_free(this->source_);
                            this->source_ = nullptr;
                        }
                    }
                );

                if (::zip_source_open(this->source_) < 0)
                {
                    LOG_ERROR("无法打开最终的 zip 数据源进行读取: {}", ::zip_error_strerror(::zip_source_error(this->source_)));
                    return ::std::nullopt;
                }

                auto source_close_guard = ::gsl::finally(
                    [this]
                    {
                        if (this->source_)
                        {
                            ::zip_source_close(this->source_);
                        }
                    }
                );

                ::zip_stat_t st {};
                ::zip_stat_init(&st);
                if (::zip_source_stat(this->source_, &st) < 0 || !(st.valid & ZIP_STAT_SIZE))
                {
                    LOG_ERROR("无法获取内存 zip 源的大小: {}", ::zip_error_strerror(::zip_source_error(this->source_)));
                    return ::std::nullopt;
                }

                kmz_data_type data(st.size);
                if (::zip_int64_t bytes_read { ::zip_source_read(this->source_, data.data(), data.size()) };
                    bytes_read < 0 || static_cast<::zip_uint64_t>(bytes_read) != st.size)
                {
                    LOG_ERROR("从内存 zip 源读取数据不完整. 错误: {}", ::zip_error_strerror(::zip_source_error(this->source_)));
                    return ::std::nullopt;
                }

                return data;
            }

            explicit operator bool(void) const noexcept
            {
                return this->archive_ != nullptr;
            }

        private:
            ::zip_t*        archive_ {};
            ::zip_source_t* source_ {};

            InMemoryZipArchive(const InMemoryZipArchive&)            = delete;
            InMemoryZipArchive& operator=(const InMemoryZipArchive&) = delete;
        };

        inline ::std::optional<::std::filesystem::path> getKmzStorageDir(void) noexcept
        {
            static ::std::filesystem::path kmz_storage_dir { plane::utils::getEXEHomePath("kmz") };

            static bool                    is_initialized = []
            {
                try
                {
                    if (!::std::filesystem::exists(kmz_storage_dir))
                    {
                        ::std::filesystem::create_directories(kmz_storage_dir);
                        LOG_INFO("创建 KMZ 存储目录: {}", kmz_storage_dir.string());

                        ::uid_t uid { ::getuid() };
                        ::gid_t gid { ::getgid() };

                        if (::chown(kmz_storage_dir.c_str(), uid, gid) != 0)
                        {
                            LOG_ERROR("无法设置目录 '{}' 的所有者: errno={}", kmz_storage_dir.string(), errno);
                            return false;
                        }

                        if (::chmod(kmz_storage_dir.c_str(), 0777) != 0)
                        {
                            LOG_ERROR("无法设置目录 '{}' 的权限为 777 : errno={}", kmz_storage_dir.string(), errno);
                            return false;
                        }
                    }
                    else
                    {
                        struct stat st {};
                        if (::stat(kmz_storage_dir.c_str(), &st) == 0)
                        {
                            ::uid_t uid { ::getuid() };
                            ::gid_t gid { ::getgid() };

                            bool    owner_ok { (st.st_uid == uid) && (st.st_gid == gid) };
                            bool    perm_ok { (st.st_mode & 0777u) == 0777 };

                            if (!owner_ok || !perm_ok)
                            {
                                if (!owner_ok && ::chown(kmz_storage_dir.c_str(), uid, gid) != 0)
                                {
                                    LOG_ERROR("无法修正目录 '{}' 的所有者", kmz_storage_dir.string());
                                    return false;
                                }
                                if (!perm_ok && ::chmod(kmz_storage_dir.c_str(), 0777) != 0)
                                {
                                    LOG_ERROR("无法修正目录 '{}' 的权限为 777", kmz_storage_dir.string());
                                    return false;
                                }
                                LOG_INFO("已修正 KMZ 目录权限和所有者");
                            }
                        }
                    }

                    return true;
                }
                catch (const ::std::filesystem::filesystem_error& e)
                {
                    LOG_ERROR("创建或访问 KMZ 目录 '{}' 失败: {}", kmz_storage_dir.string(), e.what());
                    return false;
                }
            }();

            if (!is_initialized)
            {
                LOG_ERROR("KMZ 存储目录未初始化");
                return ::std::nullopt;
            }

            return kmz_storage_dir;
        }

        inline double calculateDistance(const plane::protocol::Waypoint& wp1, const plane::protocol::Waypoint& wp2) noexcept
        {
            const double lat1_rad { wp1.WD * MATH_PI / 180.0 };
            const double lat2_rad { wp2.WD * MATH_PI / 180.0 };
            const double delta_lat_rad { (wp2.WD - wp1.WD) * MATH_PI / 180.0 };
            const double delta_lon_rad { (wp2.JD - wp1.JD) * MATH_PI / 180.0 };
            const double a { ::sin(delta_lat_rad / 2) * ::sin(delta_lat_rad / 2) +
                             ::cos(lat1_rad) * ::cos(lat2_rad) * ::sin(delta_lon_rad / 2) * ::sin(delta_lon_rad / 2) };
            const double c { 2 * ::atan2(::sqrt(a), ::sqrt(1 - a)) };
            const double horizontal { EARTH_RADIUS_M * c };
            const double vertical { wp2.GD - wp1.GD };
            return ::sqrt(horizontal * horizontal + vertical * vertical);
        }

        inline double calculateTotalDistance(const ::std::vector<plane::protocol::Waypoint>& waypoints) noexcept
        {
            double        total_distance { .0 };
            ::std::size_t size { waypoints.size() };
            for (::std::size_t i { 1 }; i < size; ++i)
            {
                total_distance += calculateDistance(waypoints[i - 1], waypoints[i]);
            }
            return total_distance;
        }

        inline double calculateTotalDuration(const ::std::vector<plane::protocol::Waypoint>& waypoints) noexcept
        {
            double        total_duration { .0 };
            ::std::size_t size { waypoints.size() };
            for (::std::size_t i { 1 }; i < size; ++i)
            {
                double distance { calculateDistance(waypoints[i - 1], waypoints[i]) };
                double speed { waypoints[i].SD };
                if (speed < 0.1)
                {
                    speed = 0.1;
                }
                total_duration += distance / speed;
            }
            return total_duration;
        }

        static ::std::string generateWaylinesWpml(const ::std::vector<plane::protocol::Waypoint>& waypoints) noexcept
        {
            plane::protocol::wpml::WaylinesWpmlFile wpml_file {};
            ::std::size_t                           size { waypoints.size() };

            wpml_file.document.missionConfig.globalTransitionalSpeed  = 10.0;
            wpml_file.document.missionConfig.droneInfo.droneEnumValue = 65'535;
            wpml_file.document.folder.distance                        = calculateTotalDistance(waypoints);
            wpml_file.document.folder.duration                        = calculateTotalDuration(waypoints);
            wpml_file.document.folder.autoFlightSpeed                 = 5.0;

            for (::std::size_t i { 0 }; i < size; ++i)
            {
                const auto&                          wp { waypoints[i] };
                plane::protocol::wpml::WpmlPlacemark placemark {};

                placemark.index                                           = static_cast<int>(i);
                placemark.point.longitude                                 = wp.JD;
                placemark.point.latitude                                  = wp.WD;
                placemark.executeHeight                                   = wp.GD;
                placemark.waypointSpeed                                   = wp.SD;

                placemark.waypointHeadingParam.waypointHeadingMode        = "smoothTransition";
                placemark.waypointHeadingParam.waypointHeadingAngle       = wp.FJPHJ;
                placemark.waypointHeadingParam.waypointHeadingAngleEnable = 1;

                if (i == 0)
                {
                    plane::protocol::wpml::WpmlActionGroup reach_ag {};
                    reach_ag.actionGroupId         = 0;
                    reach_ag.actionGroupStartIndex = 0;
                    reach_ag.actionGroupEndIndex   = 0;
                    reach_ag.actionTriggerType     = "reachPoint";

                    plane::protocol::wpml::WpmlAction gimbal_rotate {};
                    gimbal_rotate.actionActuatorFunc                             = "gimbalRotate";
                    gimbal_rotate.actionActuatorFuncParam.gimbalPitchRotateAngle = wp.YTFYJ;
                    gimbal_rotate.actionActuatorFuncParam.payloadPositionIndex   = 0;
                    reach_ag.actions.push_back(gimbal_rotate);

                    placemark.actionGroups.push_back(reach_ag);
                }

                if (i + 1 < size)
                {
                    plane::protocol::wpml::WpmlActionGroup even_ag {};
                    even_ag.actionGroupId         = static_cast<int>(i) + 1;
                    even_ag.actionGroupStartIndex = static_cast<int>(i);
                    even_ag.actionGroupEndIndex   = static_cast<int>(i) + 1;
                    even_ag.actionTriggerType     = "betweenAdjacentPoints";

                    plane::protocol::wpml::WpmlAction even_rotate {};
                    even_rotate.actionActuatorFunc                       = "gimbalEvenlyRotate";
                    even_rotate.useEvenlyRotateParam                     = true;
                    even_rotate.evenlyRotateParam.gimbalPitchRotateAngle = waypoints[i + 1].YTFYJ;
                    even_rotate.evenlyRotateParam.payloadPositionIndex   = 0;
                    even_ag.actions.push_back(even_rotate);

                    placemark.actionGroups.push_back(even_ag);
                }

                wpml_file.document.folder.placemarks.push_back(placemark);
            }

            return plane::utils::toXmlString(wpml_file);
        }

        static ::std::string generateTemplateKml(const ::std::vector<plane::protocol::Waypoint>& waypoints) noexcept
        {
            plane::protocol::kml::TemplateKmlFile kml_file {};

            if (!waypoints.empty())
            {
                kml_file.document.folder.autoFlightSpeed                = 5.0;
                kml_file.document.folder.globalHeight                   = 100.0;
                kml_file.document.missionConfig.globalTransitionalSpeed = 10.0;
            }

            ::std::size_t size { waypoints.size() };
            for (::std::size_t i { 0 }; i < size; ++i)
            {
                const auto&                         wp { waypoints[i] };
                plane::protocol::kml::WpmlPlacemark pm {};

                pm.index                                     = static_cast<int>(i);
                pm.point.longitude                           = wp.JD;
                pm.point.latitude                            = wp.WD;
                pm.ellipsoidHeight                           = wp.GD;
                pm.waypointSpeed                             = wp.SD;
                pm.gimbalPitchAngle                          = wp.YTFYJ;
                pm.waypointHeadingParam.waypointHeadingMode  = "smoothTransition";
                pm.waypointHeadingParam.waypointHeadingAngle = wp.FJPHJ;

                kml_file.document.folder.placemarks.push_back(pm);
            }

            return plane::utils::toXmlString(kml_file);
        }
    } // namespace

    ::std::optional<kmz_data_type> JsonToKmzConverter::convertWaypointsToKmz(const ::std::vector<plane::protocol::Waypoint>& waypoints) noexcept
    {
        try
        {
            g_latestKmzFilePath.clear();
            if (waypoints.empty())
            {
                LOG_ERROR("无法生成 KMZ ，航点列表为空");
                return ::std::nullopt;
            }

            ::std::string      waylines_wpml { generateWaylinesWpml(waypoints) };
            ::std::string      template_kml { generateTemplateKml(waypoints) };

            InMemoryZipArchive archive {};
            if (!archive)
            {
                LOG_ERROR("初始化内存归档器失败");
                return ::std::nullopt;
            }

            if (!archive.addFile("wpmz/waylines.wpml", waylines_wpml) || !archive.addFile("wpmz/template.kml", template_kml))
            {
                LOG_ERROR("将文件添加到内存归档失败");
                return ::std::nullopt;
            }

            auto kmz_data_opt { archive.getFinalData() };
            if (!kmz_data_opt)
            {
                LOG_ERROR("从内存归档中提取最终 KMZ 数据失败");
                return ::std::nullopt;
            }

            kmz_data_type& kmz_data { *kmz_data_opt };
            LOG_DEBUG("成功在内存中生成 KMZ 数据 ({} 字节)", kmz_data.size());

            if (plane::config::ConfigManager::getInstance().isSaveKmz())
            {
                if (auto storage_dir_opt { getKmzStorageDir() }; storage_dir_opt)
                {
                    ::std::stringstream time_ss {};
                    auto                now { ::std::chrono::system_clock::now() };
                    auto                time_t_now { ::std::chrono::system_clock::to_time_t(now) };
                    ::std::tm           tm_now {};
                    ::localtime_r(&time_t_now, &tm_now);
                    time_ss << ::std::put_time(&tm_now, "%Y%m%d_%H%M%S");
                    ::std::string           file_name { ::fmt::format("{}.kmz", time_ss.str()) };
                    ::std::filesystem::path kmz_file_path { *storage_dir_opt / file_name };

                    if (::std::ofstream out_file(kmz_file_path, ::std::ios::binary); out_file)
                    {
                        out_file.write(reinterpret_cast<const char*>(kmz_data.data()), static_cast<::std::streamsize>(kmz_data.size()));
                        out_file.close();

                        ::std::lock_guard<::std::mutex> lock(g_kmzPathMutex);
                        g_latestKmzFilePath = ::std::filesystem::absolute(kmz_file_path);
                        LOG_INFO("已成功将 KMZ 数据保存到文件: {}", g_latestKmzFilePath.string());
                    }
                    else
                    {
                        LOG_ERROR("无法写入 KMZ 文件到: {}", kmz_file_path.string());
                    }
                }
                else
                {
                    LOG_WARN("无法保存 KMZ 文件，因为存储目录无效");
                }
            }
            else
            {
                LOG_INFO("KMZ在内存中构建 ({} 字节) ", kmz_data.size());
            }

            return kmz_data_opt;
        }
        catch (const ::std::exception& e)
        {
            LOG_ERROR("生成 KMZ 时发生异常: {}", e.what());
            return ::std::nullopt;
        }
    }

    ::std::string JsonToKmzConverter::getKmzFilePath(void) noexcept
    {
        ::std::lock_guard<::std::mutex> lock(g_kmzPathMutex);
        return g_latestKmzFilePath.string();
    }
} // namespace plane::utils

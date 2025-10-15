// cy_psdk/utils/JsonConverter/JsonToKmz.cpp

#include "JsonToKmz.h"

#include "config/ConfigManager.h"
#include "protocol/KmzDataClass.h"
#include "utils/Logger.h"
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
		static _STD_FS path g_latestKmzFilePath {};
		static _STD mutex	g_kmzPathMutex {};

		class InMemoryZipArchive
		{
		public:
			explicit InMemoryZipArchive(void) noexcept
			{
				_LIBZIP zip_error_t		error {};
				_LIBZIP					zip_error_init(&error);

				this->source_ = _LIBZIP zip_source_buffer_create(nullptr, 0, 1, &error);
				if (!this->source_)
				{
					LOG_ERROR("创建 zip 内存源失败: {}", _LIBZIP zip_error_strerror(&error));
					_LIBZIP zip_error_fini(&error);
					return;
				}

				_LIBZIP					 zip_source_keep(this->source_);

				this->archive_ = _LIBZIP zip_open_from_source(this->source_, ZIP_CREATE | ZIP_TRUNCATE, &error);
				if (!this->archive_)
				{
					LOG_ERROR("从内存源打开 zip 归档失败: {}", _LIBZIP zip_error_strerror(&error));
					_LIBZIP zip_source_free(this->source_);
					this->source_ = nullptr;
				}
				_LIBZIP zip_error_fini(&error);
			}

			~InMemoryZipArchive(void) noexcept
			{
				if (this->archive_)
				{
					_LIBZIP zip_discard(this->archive_);
					this->archive_ = nullptr;
				}
				if (this->source_)
				{
					_LIBZIP zip_source_free(this->source_);
					this->source_ = nullptr;
				}
			}

			bool addFile(const _STD string& pathInZip, const _STD string& content)
			{
				if (!this->archive_)
				{
					return false;
				}

				_LIBZIP zip_source_t* content_source { _LIBZIP zip_source_buffer(this->archive_, content.c_str(), content.length(), 0) };
				if (!content_source)
				{
					LOG_ERROR("无法为 '{}' 创建 zip source: {}", pathInZip, _LIBZIP zip_strerror(this->archive_));
					return false;
				}

				if (_LIBZIP zip_file_add(this->archive_, pathInZip.c_str(), content_source, ZIP_FL_ENC_UTF_8 | ZIP_FL_OVERWRITE) < 0)
				{
					LOG_ERROR("无法将 '{}' 添加到 KMZ: {}", pathInZip, _LIBZIP zip_strerror(this->archive_));
					_LIBZIP zip_source_free(content_source);
					return false;
				}
				return true;
			}

			_STD optional<_DEFINED _KMZ_DATA_TYPE> getFinalData(void)
			{
				if (!this->source_ || !this->archive_)
				{
					return _STD nullopt;
				}

				if (_LIBZIP zip_close(this->archive_) < 0)
				{
					LOG_ERROR("关闭内存归档时出错: {}", _LIBZIP zip_error_strerror(_LIBZIP zip_get_error(this->archive_)));
					_LIBZIP zip_discard(this->archive_);
					this->archive_ = nullptr;
					return _STD nullopt;
				}
				this->archive_		   = nullptr;

				auto source_free_guard = _GSL finally(
					[this]
					{
						if (this->source_)
						{
							_LIBZIP zip_source_free(this->source_);
							this->source_ = nullptr;
						}
					});

				if (_LIBZIP zip_source_open(this->source_) < 0)
				{
					LOG_ERROR("无法打开最终的 zip 数据源进行读取: {}", _LIBZIP zip_error_strerror(_LIBZIP zip_source_error(this->source_)));
					return _STD nullopt;
				}

				auto source_close_guard = _GSL finally(
					[this]
					{
						if (this->source_)
						{
							_LIBZIP zip_source_close(this->source_);
						}
					});

				_LIBZIP zip_stat_t st {};
				_LIBZIP			   zip_stat_init(&st);
				if (_LIBZIP zip_source_stat(this->source_, &st) < 0 || !(st.valid & ZIP_STAT_SIZE))
				{
					LOG_ERROR("无法获取内存 zip 源的大小: {}", _LIBZIP zip_error_strerror(_LIBZIP zip_source_error(this->source_)));
					return _STD nullopt;
				}

				_DEFINED _KMZ_DATA_TYPE data(st.size);
				_LIBZIP zip_int64_t		bytes_read { _LIBZIP zip_source_read(this->source_, data.data(), data.size()) };

				if (bytes_read < 0 || (_LIBZIP zip_uint64_t)bytes_read != st.size)
				{
					LOG_ERROR("从内存 zip 源读取数据不完整. 错误: {}", _LIBZIP zip_error_strerror(_LIBZIP zip_source_error(this->source_)));
					return _STD nullopt;
				}

				return data;
			}

			explicit operator bool(void) const noexcept
			{
				return this->archive_ != nullptr;
			}

		private:
			_LIBZIP zip_t*		  archive_ {};
			_LIBZIP zip_source_t* source_ {};

			InMemoryZipArchive(const InMemoryZipArchive&)			 = delete;
			InMemoryZipArchive& operator=(const InMemoryZipArchive&) = delete;
		};

		inline _STD optional<_STD_FS path> getKmzStorageDir(void) noexcept
		{
			static _STD_FS path kmz_storage_dir { "/tmp/cy_psdk/kmz" };

			static bool			is_initialized = []
			{
				try
				{
					if (!_STD_FS exists(kmz_storage_dir))
					{
						_STD_FS create_directories(kmz_storage_dir);
						LOG_INFO("创建 KMZ 存储目录: {}", kmz_storage_dir.string());

						_CSTD uid_t uid { _CSTD getuid() };
						_CSTD gid_t gid { _CSTD getgid() };

						if (_CSTD chown(kmz_storage_dir.c_str(), uid, gid) != 0)
						{
							LOG_ERROR("无法设置目录 '{}' 的所有者: errno={}", kmz_storage_dir.string(), errno);
							return false;
						}

						if (_CSTD chmod(kmz_storage_dir.c_str(), 0777) != 0)
						{
							LOG_ERROR("无法设置目录 '{}' 的权限为 777: errno={}", kmz_storage_dir.string(), errno);
							return false;
						}
					}
					else
					{
						struct stat st {};
						if (_CSTD stat(kmz_storage_dir.c_str(), &st) == 0)
						{
							_CSTD uid_t uid { _CSTD getuid() };
							_CSTD gid_t gid { _CSTD getgid() };

							bool		owner_ok { (st.st_uid == uid) && (st.st_gid == gid) };
							bool		perm_ok { (st.st_mode & 0777) == 0777 };

							if (!owner_ok || !perm_ok)
							{
								if (!owner_ok && _CSTD chown(kmz_storage_dir.c_str(), uid, gid) != 0)
								{
									LOG_ERROR("无法修正目录 '{}' 的所有者", kmz_storage_dir.string());
									return false;
								}
								if (!perm_ok && _CSTD chmod(kmz_storage_dir.c_str(), 0777) != 0)
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
				catch (const _STD_FS filesystem_error& e)
				{
					LOG_ERROR("创建或访问 KMZ 目录 '{}' 失败: {}", kmz_storage_dir.string(), e.what());
					return false;
				}
			}();

			if (!is_initialized)
			{
				return _STD nullopt;
			}

			return kmz_storage_dir;
		}

		inline double calculateDistance(const plane::protocol::Waypoint& wp1, const plane::protocol::Waypoint& wp2) noexcept
		{
			const double lat1_rad { wp1.WD * _DEFINED MATH_PI / 180.0 };
			const double lat2_rad { wp2.WD * _DEFINED MATH_PI / 180.0 };
			const double delta_lat_rad { (wp2.WD - wp1.WD) * _DEFINED MATH_PI / 180.0 };
			const double delta_lon_rad { (wp2.JD - wp1.JD) * _DEFINED MATH_PI / 180.0 };
			const double a { _CSTD sin(delta_lat_rad / 2) * _CSTD sin(delta_lat_rad / 2) +
							 _CSTD cos(lat1_rad) * _CSTD cos(lat2_rad) * _CSTD sin(delta_lon_rad / 2) * _CSTD sin(delta_lon_rad / 2) };
			const double c { 2 * _CSTD atan2(_CSTD sqrt(a), _CSTD sqrt(1 - a)) };
			return _DEFINED EARTH_RADIUS_M * c;
		}

		inline double calculateTotalDistance(const _STD vector<plane::protocol::Waypoint>& waypoints) noexcept
		{
			double		total_distance { .0 };
			_STD size_t size { waypoints.size() };
			for (_STD size_t i { 1 }; i < size; ++i)
			{
				total_distance += _UNNAMED calculateDistance(waypoints[i - 1], waypoints[i]);
			}
			return total_distance;
		}

		inline double calculateTotalDuration(const _STD vector<plane::protocol::Waypoint>& waypoints) noexcept
		{
			double		total_duration { .0 };
			_STD size_t size { waypoints.size() };
			for (_STD size_t i { 1 }; i < size; ++i)
			{
				double distance { _UNNAMED calculateDistance(waypoints[i - 1], waypoints[i]) };
				double speed { waypoints[i].SD };
				if (speed < 0.1)
				{
					speed = 0.1;
				}
				total_duration += distance / speed;
			}
			return total_duration;
		}

		inline double calculateHeadingAngle(const plane::protocol::Waypoint& from, const plane::protocol::Waypoint& to) noexcept
		{
			const double delta_lon { (to.JD - from.JD) * _DEFINED MATH_PI / 180.0 };
			const double from_lat_rad { from.WD * _DEFINED MATH_PI / 180.0 };
			const double to_lat_rad { to.WD * _DEFINED MATH_PI / 180.0 };
			const double y { _CSTD sin(delta_lon) * _CSTD cos(to_lat_rad) };
			const double x { _CSTD cos(from_lat_rad) * _CSTD sin(to_lat_rad) -
							 _CSTD sin(from_lat_rad) * _CSTD cos(to_lat_rad) * _CSTD cos(delta_lon) };
			const double bearing { _CSTD atan2(y, x) * 180.0 / _DEFINED MATH_PI };
			return _CSTD fmod(bearing + 360.0, 360.0);
		}

		static _STD string generateWaylinesWpml(const _STD vector<plane::protocol::Waypoint>& waypoints) noexcept
		{
			plane::protocol::wpml::WaylinesWpmlFile wpml_file {};
			_STD size_t								size { waypoints.size() };

			wpml_file.document.missionConfig.globalTransitionalSpeed  = waypoints.empty() ? 10.0 : waypoints[0].SD;
			wpml_file.document.missionConfig.droneInfo.droneEnumValue = 78;
			wpml_file.document.folder.distance						  = _UNNAMED calculateTotalDistance(waypoints);
			wpml_file.document.folder.duration						  = _UNNAMED calculateTotalDuration(waypoints);
			wpml_file.document.folder.autoFlightSpeed				  = wpml_file.document.missionConfig.globalTransitionalSpeed;

			if (!waypoints.empty())
			{
				plane::protocol::wpml::WpmlActionGroup takeoff_group {};
				takeoff_group.actionTriggerType = "takeoff";

				plane::protocol::wpml::WpmlAction gimbal_rotate {};
				gimbal_rotate.actionId										 = 0;
				gimbal_rotate.actionActuatorFunc							 = "gimbalRotate";
				gimbal_rotate.actionActuatorFuncParam.payloadPositionIndex	 = 7;
				gimbal_rotate.actionActuatorFuncParam.gimbalYawRotateEnable	 = 1;
				gimbal_rotate.actionActuatorFuncParam.gimbalPitchRotateAngle = waypoints[0].YTFYJ.value_or(-90.0);
				takeoff_group.actions.push_back(gimbal_rotate);

				plane::protocol::wpml::WpmlAction hover {};
				hover.actionId			 = 1;
				hover.actionActuatorFunc = "hover";
				takeoff_group.actions.push_back(hover);

				plane::protocol::wpml::WpmlPlacemark first_placemark {};
				first_placemark.index			= 0;
				first_placemark.point.longitude = waypoints[0].JD;
				first_placemark.point.latitude	= waypoints[0].WD;
				first_placemark.executeHeight	= waypoints[0].GD;
				first_placemark.waypointSpeed	= waypoints[0].SD;

				if (size > 1)
				{
					first_placemark.waypointHeadingParam.waypointHeadingAngle = _UNNAMED calculateHeadingAngle(waypoints[0], waypoints[1]);
				}

				// first_placemark.actionGroups.push_back(takeoff_group);
				wpml_file.document.folder.placemarks.push_back(first_placemark);

				for (_STD size_t i { 1 }; i < size - 1; ++i)
				{
					plane::protocol::wpml::WpmlPlacemark placemark {};
					placemark.index										= i;
					placemark.point.longitude							= waypoints[i].JD;
					placemark.point.latitude							= waypoints[i].WD;
					placemark.executeHeight								= waypoints[i].GD;
					placemark.waypointSpeed								= waypoints[i].SD;
					placemark.waypointHeadingParam.waypointHeadingAngle = _UNNAMED calculateHeadingAngle(waypoints[i], waypoints[i + 1]);

					if (i == 1)
					{
						plane::protocol::wpml::WpmlActionGroup ag {};
						ag.actionGroupEndIndex = size > 2 ? static_cast<int>(size - 2) : 0;
						ag.actionTriggerType   = "betweenAdjacentPoints";

						plane::protocol::wpml::WpmlAction lock {};
						lock.actionActuatorFunc								= "gimbalAngleLock";
						lock.actionActuatorFuncParam.gimbalPitchRotateAngle = waypoints[i].YTFYJ.value_or(-90.0);
						ag.actions.push_back(lock);

						plane::protocol::wpml::WpmlAction time_lapse {};
						time_lapse.actionId										= 1;
						time_lapse.actionActuatorFunc							= "startTimeLapse";
						time_lapse.actionActuatorFuncParam.payloadPositionIndex = 7;
						ag.actions.push_back(time_lapse);

						// placemark.actionGroups.push_back(ag);
					}

					wpml_file.document.folder.placemarks.push_back(placemark);
				}

				if (size > 1)
				{
					const auto&							 last_wp { waypoints.back() };
					plane::protocol::wpml::WpmlPlacemark last_placemark {};
					last_placemark.index									 = size - 1;
					last_placemark.point.longitude							 = last_wp.JD;
					last_placemark.point.latitude							 = last_wp.WD;
					last_placemark.executeHeight							 = last_wp.GD;
					last_placemark.waypointSpeed							 = last_wp.SD;
					last_placemark.waypointHeadingParam.waypointHeadingAngle = .0;

					plane::protocol::wpml::WpmlActionGroup ag {};
					ag.actionGroupId		 = 1;
					ag.actionGroupStartIndex = static_cast<int>(waypoints.size() - 1);
					ag.actionGroupEndIndex	 = static_cast<int>(waypoints.size() - 1);
					ag.actionTriggerType	 = "reachPoint";

					plane::protocol::wpml::WpmlAction stop {};
					stop.actionActuatorFunc							  = "stopTimeLapse";
					stop.actionActuatorFuncParam.payloadPositionIndex = 7;
					ag.actions.push_back(stop);

					plane::protocol::wpml::WpmlAction unlock {};
					unlock.actionId			  = 1;
					unlock.actionActuatorFunc = "gimbalAngleUnlock";
					ag.actions.push_back(unlock);

					// slastPlacemark.actionGroups.push_back(ag);
					wpml_file.document.folder.placemarks.push_back(last_placemark);
				}
			}

			return plane::utils::toXmlString(wpml_file);
		}

		static _STD string generateTemplateKml(const _STD vector<plane::protocol::Waypoint>& waypoints,
											   const plane::protocol::WaypointPayload&		 missionInfo) noexcept
		{
			plane::protocol::kml::TemplateKmlFile kml_file {};
			_STD size_t							  size { waypoints.size() };

			if (!waypoints.empty())
			{
				kml_file.document.folder.autoFlightSpeed				= waypoints[0].SD;
				kml_file.document.folder.globalHeight					= waypoints[0].GD;
				kml_file.document.missionConfig.globalTransitionalSpeed = waypoints[0].SD;
			}

			for (_STD size_t i { 0 }; i < size; ++i)
			{
				const auto&							wp { waypoints[i] };
				plane::protocol::kml::WpmlPlacemark pm {};

				pm.index				 = i;
				pm.point.longitude		 = wp.JD;
				pm.point.latitude		 = wp.WD;

				pm.height				 = wp.GD;
				pm.ellipsoidHeight		 = wp.GD;

				pm.useGlobalHeight		 = 1;
				pm.useGlobalSpeed		 = 1;
				pm.useGlobalHeadingParam = 1;
				pm.useGlobalTurnParam	 = 1;

				kml_file.document.folder.placemarks.push_back(pm);
			}

			return plane::utils::toXmlString(kml_file);
		}
	} // namespace

	_STD optional<_DEFINED _KMZ_DATA_TYPE>
		 JsonToKmzConverter::convertWaypointsToKmz(const _STD vector<plane::protocol::Waypoint>& waypoints,
												   const plane::protocol::WaypointPayload&		 missionInfo) noexcept
	{
		try
		{
			g_latestKmzFilePath.clear();
			if (waypoints.empty())
			{
				LOG_ERROR("无法生成 KMZ ，航点列表为空。");
				return _STD nullopt;
			}

			_STD string					waylines_wpml { _UNNAMED generateWaylinesWpml(waypoints) };
			_STD string					template_kml { _UNNAMED generateTemplateKml(waypoints, missionInfo) };

			_UNNAMED InMemoryZipArchive archive {};
			if (!archive)
			{
				LOG_ERROR("初始化内存归档器失败。");
				return _STD nullopt;
			}

			if (!archive.addFile("wpmz/waylines.wpml", waylines_wpml) || !archive.addFile("wpmz/template.kml", template_kml))
			{
				return _STD nullopt;
			}

			auto kmz_data_opt { archive.getFinalData() };
			if (!kmz_data_opt)
			{
				LOG_ERROR("从内存归档中提取最终 KMZ 数据失败。");
				return _STD nullopt;
			}

			_DEFINED _KMZ_DATA_TYPE& kmz_data { *kmz_data_opt };
			LOG_DEBUG("成功在内存中生成 KMZ 数据 ({} 字节)。", kmz_data.size());

			if (plane::config::ConfigManager::getInstance().isSaveKmz())
			{
				if (auto storage_dir_opt { _UNNAMED getKmzStorageDir() }; storage_dir_opt)
				{
					_STD stringstream time_ss {};
					auto			  now { _STD_CHRONO system_clock::now() };
					auto			  time_t_now { _STD_CHRONO system_clock::to_time_t(now) };
					_STD tm			  tm_now {};
					_CSTD			  localtime_r(&time_t_now, &tm_now);
					time_ss << _STD	  put_time(&tm_now, "%Y%m%d_%H%M%S");
					_STD string		  file_name { _FMT format("{}.kmz", time_ss.str()) };
					_STD_FS path	  kmz_file_path { *storage_dir_opt / file_name };

					if (_STD ofstream out_file(kmz_file_path, _STD ios::binary); out_file)
					{
						out_file.write(reinterpret_cast<const char*>(kmz_data.data()), kmz_data.size());
						out_file.close();

						_STD lock_guard<_STD mutex>	  lock(g_kmzPathMutex);
						g_latestKmzFilePath = _STD_FS absolute(kmz_file_path);
						LOG_INFO("已成功将 KMZ 数据保存到文件: {}", g_latestKmzFilePath.string());
					}
					else
					{
						LOG_ERROR("无法写入 KMZ 文件到: {}", kmz_file_path.string());
					}
				}
				else
				{
					LOG_WARN("无法保存 KMZ 文件，因为存储目录无效。");
				}
			}

			return kmz_data_opt;
		}
		catch (const _STD exception& e)
		{
			LOG_ERROR("生成 KMZ 时发生异常: {}", e.what());
			return _STD nullopt;
		}
	}

	_STD string JsonToKmzConverter::getKmzFilePath(void) noexcept
	{
		_STD lock_guard<_STD mutex> lock(g_kmzPathMutex);
		return g_latestKmzFilePath.string();
	}
} // namespace plane::utils

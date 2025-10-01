#include "utils/JsonConverter/JsonToKmz.h"

#include <sys/stat.h>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <pwd.h>
#include <sstream>
#include <unistd.h>

#include <fmt/format.h>
#include <gsl/gsl>
#include <zip.h>

#include "protocol/KmzDataClass.h"
#include "utils/EnvironmentCheck.h"
#include "utils/Logger.h"
#include "utils/XmlUtils.h"

namespace plane::utils
{
	namespace
	{
		static _STD_FS path g_latestKmzFilePath {};

		class InMemoryZipArchive
		{
		public:
			explicit InMemoryZipArchive(void) noexcept
			{
				_LIBZIP zip_error_t		 error {};
				_LIBZIP					 zip_error_init(&error);

				this->m_source = _LIBZIP zip_source_buffer_create(nullptr, 0, 1, &error);
				if (!this->m_source)
				{
					LOG_ERROR("创建 zip 内存源失败: {}", _LIBZIP zip_error_strerror(&error));
					_LIBZIP zip_error_fini(&error);
					return;
				}

				this->m_archive = _LIBZIP zip_open_from_source(this->m_source, ZIP_CREATE | ZIP_TRUNCATE, &error);
				if (!this->m_archive)
				{
					LOG_ERROR("从内存源打开 zip 归档失败: {}", _LIBZIP zip_error_strerror(&error));
					_LIBZIP zip_source_free(m_source);
					this->m_source = nullptr;
				}
				_LIBZIP zip_error_fini(&error);
			}

			~InMemoryZipArchive(void) noexcept
			{
				if (this->m_archive)
				{
					_LIBZIP zip_discard(this->m_archive);
				}
				this->m_archive = nullptr;
				if (m_source)
				{
					_LIBZIP zip_source_free(this->m_source);
				}
				this->m_source = nullptr;
			}

			bool addFile(const _STD string& path_in_zip, const _STD string& content)
			{
				if (!this->m_archive)
				{
					return false;
				}

				_LIBZIP zip_source_t* content_source { _LIBZIP zip_source_buffer(this->m_archive, content.c_str(), content.length(), 0) };
				if (!content_source)
				{
					LOG_ERROR("无法为 '{}' 创建 zip source: {}", path_in_zip, _LIBZIP zip_strerror(this->m_archive));
					return false;
				}

				if (_LIBZIP zip_file_add(this->m_archive, path_in_zip.c_str(), content_source, ZIP_FL_ENC_UTF_8) < 0)
				{
					LOG_ERROR("无法将 '{}' 添加到 KMZ: {}", path_in_zip, _LIBZIP zip_strerror(this->m_archive));
					_LIBZIP zip_source_free(content_source);
					return false;
				}
				return true;
			}

			_STD optional<_STD vector<uint8_t>> getFinalData(void)
			{
				if (!this->m_source || !this->m_archive)
				{
					return _STD nullopt;
				}

				if (_LIBZIP zip_close(this->m_archive) < 0)
				{
					LOG_ERROR("关闭内存归档时出错: {}", _LIBZIP zip_error_strerror(_LIBZIP zip_get_error(this->m_archive)));
					_LIBZIP zip_discard(this->m_archive);
					this->m_archive = nullptr;
					_LIBZIP zip_source_free(this->m_source);
					this->m_source = nullptr;
					return _STD nullopt;
				}
				m_archive			   = nullptr;

				auto source_free_guard = _GSL finally(
					[this]
					{
						if (this->m_source)
						{
							_LIBZIP zip_source_free(this->m_source);
							this->m_source = nullptr;
						}
					});

				if (_LIBZIP zip_source_open(this->m_source) < 0)
				{
					_LIBZIP zip_error_t* err { _LIBZIP zip_source_error(this->m_source) };
					LOG_ERROR("无法打开内存 zip 源进行读取: {}", _LIBZIP zip_error_strerror(err));
					return _STD nullopt;
				}

				auto source_close_guard = _GSL finally(
					[this]
					{
						if (this->m_source)
						{
							_LIBZIP zip_source_close(this->m_source);
						}
					});

				_LIBZIP zip_stat_t st {};
				if (_LIBZIP zip_source_stat(this->m_source, &st) < 0 || !(st.valid & ZIP_STAT_SIZE))
				{
					LOG_ERROR("无法获取内存 zip 源的大小");
					return _STD nullopt;
				}

				_STD vector<uint8_t> data(st.size);
				if (_LIBZIP zip_int64_t bytes_read { _LIBZIP zip_source_read(this->m_source, data.data(), st.size) };
					bytes_read < 0 || static_cast<_LIBZIP zip_uint64_t>(bytes_read) != st.size)
				{
					LOG_ERROR("从内存 zip 源读取数据不完整");
					return _STD nullopt;
				}

				return data;
			}

			explicit operator bool() const noexcept
			{
				return this->m_archive != nullptr;
			}

		private:
			zip_t*		  m_archive { nullptr };
			zip_source_t* m_source { nullptr };
			InMemoryZipArchive(const InMemoryZipArchive&)			 = delete;
			InMemoryZipArchive& operator=(const InMemoryZipArchive&) = delete;
		};

		inline _STD optional<_STD_FS path> getKmzStorageDir(void) noexcept
		{
			static _STD_FS path kmzStorageDir { "/tmp/kmz" };

			static bool			is_initialized = []()
			{
				try
				{
					if (!_STD_FS exists(kmzStorageDir))
					{
						_STD_FS create_directories(kmzStorageDir);
						LOG_INFO("创建 KMZ 存储目录: {}", kmzStorageDir.string());

						_CSTD uid_t uid { _CSTD getuid() };
						_CSTD gid_t gid { _CSTD getgid() };

						if (_CSTD chown(kmzStorageDir.c_str(), uid, gid) != 0)
						{
							LOG_ERROR("无法设置目录 '{}' 的所有者: errno={}", kmzStorageDir.string(), errno);
							return false;
						}

						if (_CSTD chmod(kmzStorageDir.c_str(), 0777) != 0)
						{
							LOG_ERROR("无法设置目录 '{}' 的权限为 777: errno={}", kmzStorageDir.string(), errno);
							return false;
						}
					}
					else
					{
						struct stat st {};
						if (_CSTD stat(kmzStorageDir.c_str(), &st) == 0)
						{
							_CSTD uid_t uid { _CSTD getuid() };
							_CSTD gid_t gid { _CSTD getgid() };

							bool		owner_ok { (st.st_uid == uid) && (st.st_gid == gid) };
							bool		perm_ok { (st.st_mode & 0777) == 0777 };

							if (!owner_ok || !perm_ok)
							{
								if (!owner_ok && _CSTD chown(kmzStorageDir.c_str(), uid, gid) != 0)
								{
									LOG_ERROR("无法修正目录 '{}' 的所有者", kmzStorageDir.string());
									return false;
								}
								if (!perm_ok && _CSTD chmod(kmzStorageDir.c_str(), 0777) != 0)
								{
									LOG_ERROR("无法修正目录 '{}' 的权限为 777", kmzStorageDir.string());
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
					LOG_ERROR("创建或访问 KMZ 目录 '{}' 失败: {}", kmzStorageDir.string(), e.what());
					return false;
				}
			}();

			if (!is_initialized)
			{
				return _STD nullopt;
			}

			return kmzStorageDir;
		}

		inline double calculateDistance(const plane::protocol::Waypoint& wp1, const plane::protocol::Waypoint& wp2) noexcept
		{
			const double lat1Rad { wp1.WD * MATH_PI / 180.0 };
			const double lat2Rad { wp2.WD * MATH_PI / 180.0 };
			const double deltaLatRad { (wp2.WD - wp1.WD) * MATH_PI / 180.0 };
			const double deltaLonRad { (wp2.JD - wp1.JD) * MATH_PI / 180.0 };
			const double a { _CSTD sin(deltaLatRad / 2) * _CSTD sin(deltaLatRad / 2) +
							 _CSTD cos(lat1Rad) * _CSTD cos(lat2Rad) * _CSTD sin(deltaLonRad / 2) * _CSTD sin(deltaLonRad / 2) };
			const double c { 2 * _CSTD atan2(_CSTD sqrt(a), _CSTD sqrt(1 - a)) };
			return EARTH_RADIUS_M * c;
		}

		inline double calculateTotalDistance(const _STD vector<plane::protocol::Waypoint>& waypoints) noexcept
		{
			double		totalDistance { .0 };
			_STD size_t size { waypoints.size() };
			for (_STD size_t i { 1 }; i < size; ++i)
			{
				totalDistance += calculateDistance(waypoints[i - 1], waypoints[i]);
			}
			return totalDistance;
		}

		inline double calculateTotalDuration(const _STD vector<plane::protocol::Waypoint>& waypoints) noexcept
		{
			double		totalDuration { .0 };
			_STD size_t size { waypoints.size() };
			for (_STD size_t i { 1 }; i < size; ++i)
			{
				double distance { calculateDistance(waypoints[i - 1], waypoints[i]) };
				double speed { waypoints[i].SD };
				if (speed < 0.1)
				{
					speed = 0.1;
				}
				totalDuration += distance / speed;
			}
			return totalDuration;
		}

		inline double calculateHeadingAngle(const plane::protocol::Waypoint& from, const plane::protocol::Waypoint& to) noexcept
		{
			const double deltaLon { (to.JD - from.JD) * MATH_PI / 180.0 };
			const double fromLatRad { from.WD * MATH_PI / 180.0 };
			const double toLatRad { to.WD * MATH_PI / 180.0 };
			const double y { _CSTD sin(deltaLon) * _CSTD cos(toLatRad) };
			const double x { _CSTD cos(fromLatRad) * _CSTD sin(toLatRad) - _CSTD sin(fromLatRad) * _CSTD cos(toLatRad) * _CSTD cos(deltaLon) };
			const double bearing { _CSTD atan2(y, x) * 180.0 / MATH_PI };
			return _CSTD fmod(bearing + 360.0, 360.0);
		}

		static _STD string generateWaylinesWpml(const _STD vector<plane::protocol::Waypoint>& waypoints) noexcept
		{
			plane::protocol::wpml::WaylinesWpmlFile wpml_file {};
			_STD size_t								size { waypoints.size() };

			wpml_file.document.missionConfig.globalTransitionalSpeed  = waypoints.empty() ? 10.0 : waypoints[0].SD;
			wpml_file.document.missionConfig.droneInfo.droneEnumValue = 78;
			wpml_file.document.folder.distance						  = calculateTotalDistance(waypoints);
			wpml_file.document.folder.duration						  = calculateTotalDuration(waypoints);
			wpml_file.document.folder.autoFlightSpeed				  = wpml_file.document.missionConfig.globalTransitionalSpeed;

			if (!waypoints.empty())
			{
				plane::protocol::wpml::WpmlActionGroup takeoffGroup {};
				takeoffGroup.actionTriggerType = "takeoff";

				plane::protocol::wpml::WpmlAction gimbalRotate {};
				gimbalRotate.actionId										= 0;
				gimbalRotate.actionActuatorFunc								= "gimbalRotate";
				gimbalRotate.actionActuatorFuncParam.payloadPositionIndex	= 7;
				gimbalRotate.actionActuatorFuncParam.gimbalYawRotateEnable	= 1;
				gimbalRotate.actionActuatorFuncParam.gimbalPitchRotateAngle = waypoints[0].YTFYJ.value_or(-90.0);
				takeoffGroup.actions.push_back(gimbalRotate);

				plane::protocol::wpml::WpmlAction hover {};
				hover.actionId			 = 1;
				hover.actionActuatorFunc = "hover";
				takeoffGroup.actions.push_back(hover);

				plane::protocol::wpml::WpmlPlacemark firstPlacemark {};
				firstPlacemark.index		   = 0;
				firstPlacemark.point.longitude = waypoints[0].JD;
				firstPlacemark.point.latitude  = waypoints[0].WD;
				firstPlacemark.executeHeight   = waypoints[0].GD;
				firstPlacemark.waypointSpeed   = waypoints[0].SD;

				if (size > 1)
				{
					firstPlacemark.waypointHeadingParam.waypointHeadingAngle = calculateHeadingAngle(waypoints[0], waypoints[1]);
				}

				// firstPlacemark.actionGroups.push_back(takeoffGroup);
				wpml_file.document.folder.placemarks.push_back(firstPlacemark);

				for (_STD size_t i { 1 }; i < size - 1; ++i)
				{
					plane::protocol::wpml::WpmlPlacemark placemark {};
					placemark.index										= i;
					placemark.point.longitude							= waypoints[i].JD;
					placemark.point.latitude							= waypoints[i].WD;
					placemark.executeHeight								= waypoints[i].GD;
					placemark.waypointSpeed								= waypoints[i].SD;

					placemark.waypointHeadingParam.waypointHeadingAngle = calculateHeadingAngle(waypoints[i], waypoints[i + 1]);

					if (i == 1)
					{
						plane::protocol::wpml::WpmlActionGroup ag {};
						ag.actionGroupEndIndex = size > 2 ? static_cast<int>(size - 2) : 0;
						ag.actionTriggerType   = "betweenAdjacentPoints";

						plane::protocol::wpml::WpmlAction lock {};
						lock.actionActuatorFunc								= "gimbalAngleLock";
						lock.actionActuatorFuncParam.gimbalPitchRotateAngle = waypoints[i].YTFYJ.value_or(-90.0);
						ag.actions.push_back(lock);

						plane::protocol::wpml::WpmlAction timeLapse {};
						timeLapse.actionId									   = 1;
						timeLapse.actionActuatorFunc						   = "startTimeLapse";
						timeLapse.actionActuatorFuncParam.payloadPositionIndex = 7;
						ag.actions.push_back(timeLapse);

						// placemark.actionGroups.push_back(ag);
					}

					wpml_file.document.folder.placemarks.push_back(placemark);
				}

				if (size > 1)
				{
					const auto&							 lastWp { waypoints.back() };
					plane::protocol::wpml::WpmlPlacemark lastPlacemark {};
					lastPlacemark.index										= size - 1;
					lastPlacemark.point.longitude							= lastWp.JD;
					lastPlacemark.point.latitude							= lastWp.WD;
					lastPlacemark.executeHeight								= lastWp.GD;
					lastPlacemark.waypointSpeed								= lastWp.SD;
					lastPlacemark.waypointHeadingParam.waypointHeadingAngle = .0;

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
					wpml_file.document.folder.placemarks.push_back(lastPlacemark);
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

	_STD optional<_STD vector<uint8_t>> JsonToKmzConverter::convertWaypointsToKmz(const _STD vector<plane::protocol::Waypoint>& waypoints,
																				  const plane::protocol::WaypointPayload& missionInfo) noexcept
	{
		try
		{
			g_latestKmzFilePath.clear();
			if (waypoints.empty())
			{
				LOG_ERROR("无法生成 KMZ，航点列表为空。");
				return _STD nullopt;
			}

			_STD string		   waylinesWpml { _UNNAMED generateWaylinesWpml(waypoints) };
			_STD string		   templateKml { _UNNAMED generateTemplateKml(waypoints, missionInfo) };

			InMemoryZipArchive archive {};
			if (!archive)
			{
				LOG_ERROR("初始化内存归档器失败。");
				return _STD nullopt;
			}

			if (!archive.addFile("wpmz/waylines.wpml", waylinesWpml) || !archive.addFile("wpmz/template.kml", templateKml))
			{
				return _STD nullopt;
			}

			auto kmzDataOpt { archive.getFinalData() };
			if (!kmzDataOpt)
			{
				LOG_ERROR("从内存归档中提取最终 KMZ 数据失败。");
				return _STD nullopt;
			}

			_STD vector<uint8_t>& kmzData { *kmzDataOpt };
			LOG_DEBUG("成功在内存中生成 KMZ 数据 ({} 字节)。", kmzData.size());

			if (plane::utils::isSaveKmz())
			{
				if (auto storageDirOpt { _UNNAMED getKmzStorageDir() }; storageDirOpt)
				{
					_STD stringstream time_ss {};
					auto			  now { _STD_CHRONO system_clock::now() };
					auto			  time_t_now { _STD_CHRONO system_clock::to_time_t(now) };
					_STD tm			  tm_now {};
					_CSTD			  localtime_r(&time_t_now, &tm_now);
					time_ss << _STD	  put_time(&tm_now, "%Y%m%d_%H%M%S");
					_STD string		  filename { _FMT format("{}.kmz", time_ss.str()) };
					_STD_FS path	  kmzFilePath { *storageDirOpt / filename };

					if (_STD ofstream outFile(kmzFilePath, _STD ios::binary); outFile)
					{
						outFile.write(reinterpret_cast<const char*>(kmzData.data()), kmzData.size());
						outFile.close();

						g_latestKmzFilePath = _STD_FS absolute(kmzFilePath);
						LOG_INFO("已成功将 KMZ 数据保存到文件: {}", g_latestKmzFilePath.string());
					}
					else
					{
						LOG_ERROR("无法写入 KMZ 文件到: {}", kmzFilePath.string());
					}
				}
				else
				{
					LOG_WARN("无法保存 KMZ 文件，因为存储目录无效。");
				}
			}

			return kmzDataOpt;
		}
		catch (const _STD exception& e)
		{
			LOG_ERROR("生成 KMZ 时发生异常: {}", e.what());
			return _STD nullopt;
		}
	}

	_STD string JsonToKmzConverter::getKmzFilePath(void) noexcept
	{
		return g_latestKmzFilePath.string();
	}
} // namespace plane::utils

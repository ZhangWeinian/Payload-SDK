#include "utils/JsonConverter/JsonToKmz.h"

#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#include <fmt/format.h>
#include <zip.h>

#include "protocol/KmzDataClass.h"
#include "utils/Logger.h"
#include "utils/XmlUtils.h"

namespace plane::utils
{
	namespace
	{
		static _STD_FS path g_latestKmzFilePath {};

		class ZipArchive
		{
		public:
			explicit ZipArchive(const _STD_FS path& filepath)
			{
				int					error { 0 };
				m_archive = _LIBZIP zip_open(filepath.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &error);
				if (!m_archive)
				{
					_LIBZIP zip_error_t ziperror {};
					_LIBZIP				zip_error_init_with_code(&ziperror, error);
					LOG_ERROR("无法创建或打开 KMZ 文件 '{}': {}", filepath.string(), _LIBZIP zip_error_strerror(&ziperror));
					_LIBZIP zip_error_fini(&ziperror);
				}
			}

			~ZipArchive(void) noexcept
			{
				if (m_archive)
				{
					if (_LIBZIP zip_close(m_archive) < 0)
					{
						LOG_ERROR("关闭 KMZ 文件时出错: {}", _LIBZIP zip_strerror(m_archive));
					}
				}
			}

			bool addFile(const _STD string& path_in_zip, const _STD string& content)
			{
				if (!m_archive)
				{
					return false;
				}

				_LIBZIP zip_source_t* source { _LIBZIP zip_source_buffer(m_archive, content.c_str(), content.length(), 0) };
				if (!source)
				{
					LOG_ERROR("无法为 '{}' 创建 zip source: {}", path_in_zip, _LIBZIP zip_strerror(m_archive));
					return false;
				}

				if (_LIBZIP zip_file_add(m_archive, path_in_zip.c_str(), source, ZIP_FL_ENC_UTF_8) < 0)
				{
					LOG_ERROR("无法将 '{}' 添加到 KMZ: {}", path_in_zip, _LIBZIP zip_strerror(m_archive));
					_LIBZIP zip_source_free(source);
					return false;
				}
				return true;
			}

			explicit operator bool(void) const noexcept
			{
				return m_archive != nullptr;
			}

		private:
			_LIBZIP zip_t* m_archive { nullptr };
			ZipArchive(const ZipArchive&)			 = delete;
			ZipArchive& operator=(const ZipArchive&) = delete;
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
			double totalDistance { 0.0 };
			for (_STD size_t i { 1 }; i < waypoints.size(); ++i)
			{
				totalDistance += calculateDistance(waypoints[i - 1], waypoints[i]);
			}
			return totalDistance;
		}

		inline double calculateTotalDuration(const _STD vector<plane::protocol::Waypoint>& waypoints) noexcept
		{
			double totalDuration { 0.0 };
			for (_STD size_t i { 1 }; i < waypoints.size(); ++i)
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

				firstPlacemark.actionGroups.push_back(takeoffGroup);
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

						placemark.actionGroups.push_back(ag);
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

					lastPlacemark.actionGroups.push_back(ag);
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

	bool JsonToKmzConverter::convertWaypointsToKmz(const _STD vector<plane::protocol::Waypoint>& waypoints,
												   const plane::protocol::WaypointPayload&		 missionInfo) noexcept
	{
		g_latestKmzFilePath.clear();

		auto storageDirOpt { getKmzStorageDir() };
		if (waypoints.empty() || !storageDirOpt)
		{
			LOG_ERROR("无法生成 KMZ 文件, 因为航点列表为空或存储目录无效。");
			return false;
		}

		const _STD_FS path& storageDir { *storageDirOpt };
		_STD string			missionId { missionInfo.RWID.value_or("mission_unknown") };
		auto				now { _STD_CHRONO system_clock::now() };
		auto				time_t_now { _STD_CHRONO system_clock::to_time_t(now) };
		_STD tm				tm_now {};
		_CSTD				localtime_r(&time_t_now, &tm_now);
		_STD stringstream	time_ss {};
		time_ss << _STD		put_time(&tm_now, "%Y%m%d_%H%M%S");
		_STD string			filename { _FMT format("{}.kmz", time_ss.str()) };
		_STD_FS path		kmzFilePath { storageDir / filename };

		LOG_DEBUG("开始生成 KMZ 文件, 任务 ID: {}, 路径: {}", missionId, kmzFilePath.string());

		_STD string waylinesWpml { generateWaylinesWpml(waypoints) };
		_STD string templateKml { generateTemplateKml(waypoints, missionInfo) };

		ZipArchive	archive(kmzFilePath);
		if (!archive)
		{
			return false;
		}

		if (!archive.addFile("wpmz/waylines.wpml", waylinesWpml))
		{
			return false;
		}

		if (!archive.addFile("wpmz/template.kml", templateKml))
		{
			return false;
		}

		g_latestKmzFilePath = _STD_FS absolute(kmzFilePath);
		LOG_INFO("成功创建 KMZ 文件: {}", g_latestKmzFilePath.string());
		return true;
	}

	_STD string JsonToKmzConverter::getKmzFilePath(void) noexcept
	{
		return g_latestKmzFilePath.string();
	}
} // namespace plane::utils

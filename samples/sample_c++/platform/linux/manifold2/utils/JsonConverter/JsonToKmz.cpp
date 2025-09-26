// manifold2/utils/JsonConverter/JsonToKmz.cpp

#include "utils/JsonConverter/JsonToKmz.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#include <fmt/format.h>
#include <zip.h>

#include "protocol/KmzDataClass.h"
#include "utils/Logger.h"

namespace plane::utils
{
	namespace
	{
		static _STD_FS path		   g_latestKmzFilePath {};
		static _STD_FS path		   g_kmzStorageDir {};

		inline const _STD_FS path& getKmzStorageDir(void) noexcept
		{
			if (g_kmzStorageDir.empty())
			{
				_STD array<char, PATH_MAX> exePath {};

				if (ssize_t count { _CSTD readlink("/proc/self/exe", exePath.data(), exePath.size() - 1) }; count != -1)
				{
					exePath[count] = '\0';
					_STD_FS path executablePath(exePath.data());
					_STD_FS path executableDir { executablePath.parent_path() };
					g_kmzStorageDir = executableDir / "kmz_files";
				}
				else
				{
					LOG_WARN("无法获取可执行文件路径, 使用当前工作目录");
					g_kmzStorageDir = _STD_FS absolute(_STD_FS current_path() / "kmz_files");
				}

				try
				{
					if (!_STD_FS exists(g_kmzStorageDir))
					{
						_STD_FS create_directories(g_kmzStorageDir);
						LOG_INFO("创建 KMZ 存储目录: {}", g_kmzStorageDir.string());
					}

					_STD_FS path testFile { g_kmzStorageDir / "test_write.tmp" };
					if (_STD ofstream ofs(testFile); !ofs)
					{
						LOG_ERROR("KMZ 目录 '{}' 不可写", g_kmzStorageDir.string());
						g_kmzStorageDir = "";
					}
					else
					{
						ofs.close();
						_STD_FS remove(testFile);
					}
				}
				catch (const _STD_FS filesystem_error& e)
				{
					LOG_ERROR("创建或访问 KMZ 目录 '{}' 失败: {}", g_kmzStorageDir.string(), e.what());
					g_kmzStorageDir = "";
				}
			}

			return g_kmzStorageDir;
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
			for (size_t i { 1 }; i < waypoints.size(); ++i)
			{
				totalDistance += calculateDistance(waypoints[i - 1], waypoints[i]);
			}
			return totalDistance;
		}

		inline double calculateTotalDuration(const _STD vector<plane::protocol::Waypoint>& waypoints) noexcept
		{
			double totalDuration { 0.0 };
			for (size_t i { 1 }; i < waypoints.size(); ++i)
			{
				double distance { calculateDistance(waypoints[i - 1], waypoints[i]) };
				double speed { waypoints[i].SD };
				if (speed < 0.1)
				{
					speed = 5.0;
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
			plane::protocol::WpmlRoot wpml {};
			size_t					  size { waypoints.size() };

			wpml.document.missionConfig.globalTransitionalSpeed = waypoints.empty() ? 5.0 : waypoints[0].SD;
			wpml.document.folder.distance						= _STD stod(_FMT format("{:.2f}", calculateTotalDistance(waypoints)));
			wpml.document.folder.duration						= _STD stod(_FMT format("{:.2f}", calculateTotalDuration(waypoints)));
			wpml.document.folder.autoFlightSpeed				= wpml.document.missionConfig.globalTransitionalSpeed;

			if (!waypoints.empty())
			{
				plane::protocol::WpmlActionGroup takeoffGroup {};
				takeoffGroup.actionTriggerType = "takeoff";

				plane::protocol::WpmlAction gimbalRotate {};
				gimbalRotate.actionId									   = 0;
				gimbalRotate.actionActuatorFunc							   = "gimbalRotate";
				gimbalRotate.actionActuatorFuncParam.payloadPositionIndex  = 0;
				gimbalRotate.actionActuatorFuncParam.gimbalYawRotateEnable = 1;
				if (waypoints[0].YTFYJ)
				{
					gimbalRotate.actionActuatorFuncParam.gimbalPitchRotateAngle = _STD stod(_FMT format("{:.6f}", waypoints[0].YTFYJ.value()));
				}
				else
				{
					gimbalRotate.actionActuatorFuncParam.gimbalPitchRotateAngle = -90.0;
				}
				takeoffGroup.actions.push_back(gimbalRotate);

				plane::protocol::WpmlAction hover {};
				hover.actionId			 = 1;
				hover.actionActuatorFunc = "hover";
				takeoffGroup.actions.push_back(hover);

				plane::protocol::WpmlPlacemark firstPlacemark {};
				firstPlacemark.point.longitude = waypoints[0].JD;
				firstPlacemark.point.latitude  = waypoints[0].WD;
				firstPlacemark.executeHeight   = _STD stod(_FMT format("{:.12f}", waypoints[0].GD));
				firstPlacemark.waypointSpeed   = _STD stod(_FMT format("{:.12f}", waypoints[0].SD));

				if (size > 1)
				{
					firstPlacemark.waypointHeadingParam.waypointHeadingAngle =
						_STD stod(_FMT format("{:.6f}", calculateHeadingAngle(waypoints[0], waypoints[1])));
				}

				firstPlacemark.actionGroups.push_back(takeoffGroup);

				wpml.document.folder.placemarks.push_back(firstPlacemark);

				for (size_t i { 1 }; i < size - 1; ++i)
				{
					plane::protocol::WpmlPlacemark placemark {};
					placemark.point.longitude = waypoints[i].JD;
					placemark.point.latitude  = waypoints[i].WD;
					placemark.executeHeight	  = _STD stod(_FMT format("{:.12f}", waypoints[i].GD));
					placemark.waypointSpeed	  = _STD stod(_FMT format("{:.12f}", waypoints[i].SD));

					placemark.waypointHeadingParam.waypointHeadingAngle =
						_STD stod(_FMT format("{:.6f}", calculateHeadingAngle(waypoints[i], waypoints[i + 1])));

					if (i == 1)
					{
						plane::protocol::WpmlActionGroup ag {};
						ag.actionGroupEndIndex = size > 2 ? static_cast<int>(size - 2) : 0;
						ag.actionTriggerType   = "betweenAdjacentPoints";

						plane::protocol::WpmlAction lock {};
						lock.actionActuatorFunc = "gimbalAngleLock";
						if (waypoints[i].YTFYJ.has_value())
						{
							lock.actionActuatorFuncParam.gimbalPitchRotateAngle = _STD stod(_FMT format("{:.6f}", waypoints[i].YTFYJ.value()));
						}
						else
						{
							gimbalRotate.actionActuatorFuncParam.gimbalPitchRotateAngle = -90.0;
						}
						ag.actions.push_back(lock);

						plane::protocol::WpmlAction timeLapse {};
						timeLapse.actionId									   = 1;
						timeLapse.actionActuatorFunc						   = "startTimeLapse";
						timeLapse.actionActuatorFuncParam.payloadPositionIndex = 0;
						ag.actions.push_back(timeLapse);

						placemark.actionGroups.push_back(ag);
					}

					wpml.document.folder.placemarks.push_back(placemark);
				}

				if (size > 1)
				{
					const auto&					   lastWp { waypoints.back() };
					plane::protocol::WpmlPlacemark lastPlacemark {};
					lastPlacemark.point.longitude							= lastWp.JD;
					lastPlacemark.point.latitude							= lastWp.WD;
					lastPlacemark.executeHeight								= _STD stod(_FMT format("{:.12f}", lastWp.GD));
					lastPlacemark.waypointSpeed								= _STD stod(_FMT format("{:.12f}", lastWp.SD));
					lastPlacemark.waypointHeadingParam.waypointHeadingAngle = 0.0;

					plane::protocol::WpmlActionGroup ag {};
					ag.actionGroupId		 = 1;
					ag.actionGroupStartIndex = static_cast<int>(waypoints.size() - 1);
					ag.actionGroupEndIndex	 = static_cast<int>(waypoints.size() - 1);
					ag.actionTriggerType	 = "reachPoint";

					plane::protocol::WpmlAction stop {};
					stop.actionActuatorFunc							  = "stopTimeLapse";
					stop.actionActuatorFuncParam.payloadPositionIndex = 0;
					ag.actions.push_back(stop);

					plane::protocol::WpmlAction unlock {};
					unlock.actionId			  = 1;
					unlock.actionActuatorFunc = "gimbalAngleUnlock";
					ag.actions.push_back(unlock);

					lastPlacemark.actionGroups.push_back(ag);

					wpml.document.folder.placemarks.push_back(lastPlacemark);
				}
			}

			pugi::xml_document doc {};
			wpml.toXml(doc);
			_STD ostringstream oss {};
			doc.save(oss, "  ", pugi::format_default, pugi::encoding_utf8);
			return oss.str();
		}

		static _STD string generateTemplateKml(void) noexcept
		{
			plane::protocol::TemplateKml kml {};
			pugi::xml_document			 doc {};
			kml.toXml(doc);
			_STD ostringstream oss {};
			doc.save(oss, "  ", pugi::format_default, pugi::encoding_utf8);
			return oss.str();
		}
	} // namespace

	bool JsonToKmzConverter::convertWaypointsToKmz(const _STD vector<plane::protocol::Waypoint>& waypoints,
												   const plane::protocol::WaypointPayload&		 missionInfo) noexcept
	{
		g_latestKmzFilePath = "";

		const _STD_FS path& storageDir { getKmzStorageDir() };
		if (waypoints.empty() || storageDir.empty())
		{
			LOG_ERROR("无法生成 KMZ 文件, 因为航点列表为空或存储目录无效。");
			return false;
		}

		_STD string		  missionId { missionInfo.RWID.value_or("mission_unknown") };
		auto			  now { _STD_CHRONO system_clock::now() };
		auto			  time_t_now { _STD_CHRONO system_clock::to_time_t(now) };
		_STD tm			  tm_now { *_STD localtime(&time_t_now) };
		_STD stringstream time_ss {};
		time_ss << _STD	  put_time(&tm_now, "%Y%m%d_%H%M%S");
		_STD string		  filename { _FMT format("{}.kmz", time_ss.str()) };
		_STD_FS path	  kmzFilePath { storageDir / filename };

		LOG_DEBUG("开始生成 KMZ 文件, 任务 ID: {}, 路径: {}", missionId, kmzFilePath.string());

		_STD string waylinesWpml { generateWaylinesWpml(waypoints) };
		_STD string templateKml { generateTemplateKml() };

		int			error { 0 };
		zip_t*		archive { _LIBZIP zip_open(kmzFilePath.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &error) };
		if (!archive)
		{
			zip_error_t ziperror {};
			_LIBZIP		zip_error_init_with_code(&ziperror, error);
			LOG_ERROR("无法创建或打开 KMZ 文件 '{}': {}", kmzFilePath.string(), zip_error_strerror(&ziperror));
			_LIBZIP zip_error_fini(&ziperror);
			return false;
		}

		zip_source_t* waylines_source { _LIBZIP zip_source_buffer(archive, waylinesWpml.c_str(), waylinesWpml.length(), 0) };
		if (!waylines_source)
		{
			LOG_ERROR("无法为 waylines.wpml 创建 zip source: {}", _LIBZIP zip_strerror(archive));
			_LIBZIP zip_close(archive);
			return false;
		}
		if (_LIBZIP zip_file_add(archive, "wpmz/waylines.wpml", waylines_source, ZIP_FL_ENC_UTF_8) < 0)
		{
			LOG_ERROR("无法将 'wpmz/waylines.wpml' 添加到 KMZ: {}", _LIBZIP zip_strerror(archive));
			_LIBZIP zip_source_free(waylines_source);
			_LIBZIP zip_close(archive);
			return false;
		}

		zip_source_t* template_source { _LIBZIP zip_source_buffer(archive, templateKml.c_str(), templateKml.length(), 0) };
		if (!template_source)
		{
			LOG_ERROR("无法为 template.kml 创建 zip source: {}", _LIBZIP zip_strerror(archive));
			_LIBZIP zip_close(archive);
			return false;
		}
		if (_LIBZIP zip_file_add(archive, "wpmz/template.kml", template_source, ZIP_FL_ENC_UTF_8) < 0)
		{
			LOG_ERROR("无法将 'wpmz/template.kml' 添加到 KMZ: {}", _LIBZIP zip_strerror(archive));
			_LIBZIP zip_source_free(template_source);
			_LIBZIP zip_close(archive);
			return false;
		}

		if (_LIBZIP zip_close(archive) < 0)
		{
			LOG_ERROR("关闭 KMZ 文件时出错: {}", _LIBZIP zip_strerror(archive));
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

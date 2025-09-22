#include "utils/JsonConverter/JsonToKmz.h"

#include "protocol/KmzDataClass.h"
#include "utils/Logger/Logger.h"

#include "fmt/format.h"
#include "pugixml.hpp"
#include "zip.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace plane::utils
{
	constexpr inline double MATH_PI		   = 3.14159265358979323846;
	constexpr inline double EARTH_RADIUS_M = 6'371'000.0;

	namespace fs						   = _STD	  filesystem;

	namespace
	{
		static fs::path		   g_latestKmzFilePath {};
		static fs::path		   g_kmzStorageDir {};

		inline const fs::path& getKmzStorageDir(void) noexcept
		{
			if (g_kmzStorageDir.empty())
			{
				char exePath[PATH_MAX] {};

				if (ssize_t count { readlink("/proc/self/exe", exePath, sizeof(exePath) - 1) }; count != -1)
				{
					exePath[count] = '\0';
					fs::path executablePath(exePath);
					fs::path executableDir { executablePath.parent_path() };
					g_kmzStorageDir = executableDir / "kmz_files";
				}
				else
				{
					LOG_WARN("无法获取可执行文件路径, 使用当前工作目录");
					g_kmzStorageDir = fs::absolute(fs::current_path() / "kmz_files");
				}

				try
				{
					if (!fs::exists(g_kmzStorageDir))
					{
						fs::create_directories(g_kmzStorageDir);
						LOG_INFO("创建 KMZ 存储目录: {}", g_kmzStorageDir.string());
					}

					fs::path testFile { g_kmzStorageDir / "test_write.tmp" };
					if (_STD ofstream ofs(testFile); !ofs)
					{
						LOG_ERROR("KMZ 目录 '{}' 不可写", g_kmzStorageDir.string());
						g_kmzStorageDir = "";
					}
					else
					{
						ofs.close();
						fs::remove(testFile);
					}
				}
				catch (const fs::filesystem_error& e)
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
			const double a { sin(deltaLatRad / 2) * sin(deltaLatRad / 2) +
							 cos(lat1Rad) * cos(lat2Rad) * sin(deltaLonRad / 2) * sin(deltaLonRad / 2) };
			const double c { 2 * atan2(sqrt(a), sqrt(1 - a)) };
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
			const double y { sin(deltaLon) * cos(toLatRad) };
			const double x { cos(fromLatRad) * sin(toLatRad) - sin(fromLatRad) * cos(toLatRad) * cos(deltaLon) };
			const double bearing { atan2(y, x) * 180.0 / MATH_PI };
			return fmod(bearing + 360.0, 360.0);
		}

		static _STD string generateWaylinesWpml(const _STD vector<plane::protocol::Waypoint>& waypoints) noexcept
		{
			plane::protocol::WpmlRoot wpmlRoot {};
			wpmlRoot.document.missionConfig.globalTransitionalSpeed = waypoints.empty() ? 5.0 : waypoints[0].SD;
			wpmlRoot.document.folder.templateId						= 0;
			wpmlRoot.document.folder.executeHeightMode				= "relativeToStartPoint";
			wpmlRoot.document.folder.waylineId						= 0;
			wpmlRoot.document.folder.distance						= _STD stod(fmt::format("{:.2f}", calculateTotalDistance(waypoints)));
			wpmlRoot.document.folder.duration						= _STD stod(fmt::format("{:.2f}", calculateTotalDuration(waypoints)));
			wpmlRoot.document.folder.autoFlightSpeed				= wpmlRoot.document.missionConfig.globalTransitionalSpeed;

			if (!waypoints.empty())
			{
				plane::protocol::WpmlActionGroup startGroup {};
				startGroup.groupId	   = 0;
				startGroup.startIndex  = 0;
				startGroup.endIndex	   = 0;
				startGroup.mode		   = "sequence";
				startGroup.triggerType = "takeoff";

				plane::protocol::WpmlAction gimbalRotate {};
				gimbalRotate.actionId	  = 0;
				gimbalRotate.actuatorFunc = "gimbalRotate";

				plane::protocol::WpmlActionActuatorFuncParam gimbalParam {};
				gimbalParam.gimbalPitchRotateAngle	= -90.00;
				gimbalParam.payloadPositionIndex	= 0;
				gimbalParam.gimbalHeadingYawBase	= "aircraft";
				gimbalParam.gimbalRotateMode		= "absoluteAngle";
				gimbalParam.gimbalPitchRotateEnable = 1;
				gimbalParam.gimbalRollRotateEnable	= 0;
				gimbalParam.gimbalRollRotateAngle	= 0.00;
				gimbalParam.gimbalYawRotateEnable	= 1;
				gimbalParam.gimbalYawRotateAngle	= 0.00;
				gimbalParam.gimbalRotateTimeEnable	= 0;
				gimbalParam.gimbalRotateTime		= 10;
				gimbalRotate.actuatorFuncParam		= gimbalParam;
				startGroup.actions.push_back(gimbalRotate);

				plane::protocol::WpmlAction hover {};
				hover.actionId	   = 1;
				hover.actuatorFunc = "hover";

				plane::protocol::WpmlActionActuatorFuncParam hoverParam {};
				hoverParam.hoverTime	= 0.5;
				hover.actuatorFuncParam = hoverParam;
				startGroup.actions.push_back(hover);

				wpmlRoot.document.folder.startActionGroups.push_back(startGroup);
			}

			size_t i { 0 };
			size_t size { waypoints.size() };
			for (const auto& wp : waypoints)
			{
				plane::protocol::WpmlPlacemark placemark {};
				placemark.point.longitude		   = wp.JD;
				placemark.point.latitude		   = wp.WD;
				placemark.executeHeight			   = _STD stod(fmt::format("{:.12f}", wp.GD));
				placemark.waypointSpeed			   = _STD stod(fmt::format("{:.12f}", wp.SD));

				placemark.headingParam.headingMode = "followWayline";
				placemark.headingParam.headingAngle =
					(i < size - 1) ? _STD stod(fmt::format("{:.6f}", calculateHeadingAngle(wp, waypoints[i + 1]))) : 0.0;
				placemark.headingParam.poiPoint			  = "0.000000,0.000000,0.000000";
				placemark.headingParam.headingAngleEnable = 1;
				placemark.headingParam.headingPathMode	  = "followBadArc";
				placemark.headingParam.headingPoiIndex	  = 0;

				if ((i == 0) || (i == size - 1))
				{
					placemark.turnParam.turnMode		= "toPointAndStopWithDiscontinuityCurvature";
					placemark.turnParam.turnDampingDist = 0;
				}
				else
				{
					placemark.turnParam.turnMode		= "coordinateTurn";
					placemark.turnParam.turnDampingDist = 10;
				}

				placemark.useStraightLine					  = 1;
				placemark.gimbalHeadingParam.gimbalPitchAngle = 0;
				placemark.gimbalHeadingParam.gimbalYawAngle	  = 0;
				placemark.isRisky							  = 0;
				placemark.workType							  = 0;

				// 首航点 actionGroup
				if (i == 0)
				{
					plane::protocol::WpmlActionGroup ag;
					ag.groupId	   = 0;
					ag.startIndex  = 0;
					ag.endIndex	   = size > 1 ? size - 2 : 0;
					ag.mode		   = "sequence";
					ag.triggerType = "betweenAdjacentPoints";

					plane::protocol::WpmlAction lock;
					lock.actionId	  = 0;
					lock.actuatorFunc = "gimbalAngleLock";
					ag.actions.push_back(lock);

					plane::protocol::WpmlAction timeLapse;
					timeLapse.actionId	   = 1;
					timeLapse.actuatorFunc = "startTimeLapse";

					plane::protocol::WpmlActionActuatorFuncParam param;
					param.payloadPositionIndex		= 0;
					param.useGlobalPayloadLensIndex = 0;
					param.payloadLensIndex			= "visible";
					param.minShootInterval			= 2.0;
					timeLapse.actuatorFuncParam		= param;
					ag.actions.push_back(timeLapse);

					placemark.actionGroups.push_back(ag);
				}

				// 末航点 actionGroup
				if (i == size - 1)
				{
					protocol::WpmlActionGroup ag;
					ag.groupId	   = 1;
					ag.startIndex  = i;
					ag.endIndex	   = i;
					ag.mode		   = "sequence";
					ag.triggerType = "reachPoint";

					protocol::WpmlAction stop;
					stop.actionId	  = 0;
					stop.actuatorFunc = "stopTimeLapse";
					protocol::WpmlActionActuatorFuncParam param;
					param.payloadPositionIndex = 0;
					param.payloadLensIndex	   = "visible";
					stop.actuatorFuncParam	   = param;
					ag.actions.push_back(stop);

					protocol::WpmlAction unlock;
					unlock.actionId		= 1;
					unlock.actuatorFunc = "gimbalAngleUnlock";
					ag.actions.push_back(unlock);

					placemark.actionGroups.push_back(ag);
				}

				wpmlRoot.document.folder.placemarks.push_back(placemark);
				++i;
			}

			pugi::xml_document doc {};
			wpmlRoot.toXml(doc);
			_STD ostringstream oss {};
			doc.save(oss, "  ", pugi::format_default, pugi::encoding_utf8);
			return oss.str();
		}

		static _STD string generateTemplateKml(void) noexcept
		{
			plane::protocol::TemplateKml tpl {};
			pugi::xml_document			 doc {};
			tpl.toXml(doc);

			_STD ostringstream oss {};
			doc.save(oss, "  ", pugi::format_default, pugi::encoding_utf8);

			return oss.str();
		}
	} // namespace

	bool JsonToKmzConverter::convertWaypointsToKmz(const _STD vector<protocol::Waypoint>& waypoints,
												   const protocol::WaypointPayload&		  missionInfo) noexcept
	{
		g_latestKmzFilePath = "";

		const fs::path& storageDir { getKmzStorageDir() };
		if (waypoints.empty() || storageDir.empty())
		{
			LOG_ERROR("无法生成 KMZ 文件, 因为航点列表为空或存储目录无效。");
			return false;
		}

		_STD string		  missionId { missionInfo.RWID.value_or("mission_unknown") };
		auto			  now { _STD chrono::system_clock::now() };
		auto			  time_t_now { _STD chrono::system_clock::to_time_t(now) };
		_STD tm			  tm_now { *_STD localtime(&time_t_now) };
		_STD stringstream time_ss {};
		time_ss << _STD	  put_time(&tm_now, "%Y%m%d_%H%M%S");
		_STD string		  filename { fmt::format("{}.kmz", time_ss.str()) };
		fs::path		  kmzFilePath { storageDir / filename };

		LOG_DEBUG("开始生成 KMZ 文件, 任务 ID: {}, 路径: {}", missionId, kmzFilePath.string());

		_STD string waylinesWpml { generateWaylinesWpml(waypoints) };
		_STD string templateKml { generateTemplateKml() };

		int			error { 0 };
		zip_t*		archive { zip_open(kmzFilePath.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &error) };
		if (!archive)
		{
			zip_error_t ziperror;
			zip_error_init_with_code(&ziperror, error);
			LOG_ERROR("无法创建或打开 KMZ 文件 '{}': {}", kmzFilePath.string(), zip_error_strerror(&ziperror));
			zip_error_fini(&ziperror);
			return false;
		}

		zip_source_t* waylines_source { zip_source_buffer(archive, waylinesWpml.c_str(), waylinesWpml.length(), 0) };
		if (!waylines_source)
		{
			LOG_ERROR("无法创建 waylines.wpml 的 ZIP 源");
			zip_close(archive);
			return false;
		}
		if (zip_file_add(archive, "waylines.wpml", waylines_source, ZIP_FL_ENC_UTF_8) < 0)
		{
			LOG_ERROR("无法将 'waylines.wpml' 添加到 KMZ: {}", zip_strerror(archive));
			zip_source_free(waylines_source);
			zip_close(archive);
			return false;
		}

		zip_source_t* template_source { zip_source_buffer(archive, templateKml.c_str(), templateKml.length(), 0) };
		if (!template_source)
		{
			LOG_ERROR("无法创建 template.kml 的 ZIP 源");
			zip_close(archive);
			return false;
		}
		if (zip_file_add(archive, "template.kml", template_source, ZIP_FL_ENC_UTF_8) < 0)
		{
			LOG_ERROR("无法将 'template.kml' 添加到 KMZ: {}", zip_strerror(archive));
			zip_source_free(template_source);
			zip_close(archive);
			return false;
		}
		if (zip_close(archive) < 0)
		{
			LOG_ERROR("关闭 KMZ 文件时出错: {}", zip_strerror(archive));
			return false;
		}

		g_latestKmzFilePath = fs::absolute(kmzFilePath);

		LOG_INFO("成功创建 KMZ 文件: {}", g_latestKmzFilePath.string());
		return true;
	}

	_STD string JsonToKmzConverter::getKmzFilePath(void) noexcept
	{
		return g_latestKmzFilePath.string();
	}
} // namespace plane::utils

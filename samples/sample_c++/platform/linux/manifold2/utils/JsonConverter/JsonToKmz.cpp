#include "utils/JsonConverter/JsonToKmz.h"

#include "utils/Logger/Logger.h"

#include "zip.h"

#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>

#ifndef M_PI
	#define M_PI 3.14159265358979323846
#endif

namespace plane::utils
{
	namespace fs = std::filesystem;

	namespace
	{
		fs::path		g_latestKmzFilePath {};
		fs::path		g_kmzStorageDir {};

		const fs::path& getKmzStorageDir()
		{
			if (g_kmzStorageDir.empty())
			{
				fs::path executable_dir { "." };
				g_kmzStorageDir = executable_dir / "kmz_files";

				try
				{
					if (!fs::exists(g_kmzStorageDir))
					{
						fs::create_directories(g_kmzStorageDir);
						LOG_INFO("创建 KMZ 存储目录: {}", g_kmzStorageDir.string());
					}
				}
				catch (const fs::filesystem_error& e)
				{
					LOG_ERROR("创建 KMZ 目录 '{}' 失败: {}", g_kmzStorageDir.string(), e.what());
					g_kmzStorageDir = "";
				}
			}

			return g_kmzStorageDir;
		}

		double calculateDistance(const protocol::Waypoint& wp1, const protocol::Waypoint& wp2)
		{
			const double R { 6'371'000 };
			const double lat1Rad { wp1.WD * M_PI / 180.0 };
			const double lat2Rad { wp2.WD * M_PI / 180.0 };
			const double deltaLatRad { (wp2.WD - wp1.WD) * M_PI / 180.0 };
			const double deltaLonRad { (wp2.JD - wp1.JD) * M_PI / 180.0 };
			const double a { sin(deltaLatRad / 2) * sin(deltaLatRad / 2) +
							 cos(lat1Rad) * cos(lat2Rad) * sin(deltaLonRad / 2) * sin(deltaLonRad / 2) };
			const double c { 2 * atan2(sqrt(a), sqrt(1 - a)) };
			return R * c;
		}

		double calculateTotalDistance(const std::vector<protocol::Waypoint>& waypoints)
		{
			double totalDistance { 0.0 };
			for (size_t i { 1 }; i < waypoints.size(); ++i)
			{
				totalDistance += calculateDistance(waypoints[i - 1], waypoints[i]);
			}
			return totalDistance;
		}

		double calculateTotalDuration(const std::vector<protocol::Waypoint>& waypoints)
		{
			double totalDuration { 0.0 };
			for (size_t i { 1 }; i < waypoints.size(); ++i)
			{
				double distance { calculateDistance(waypoints[i - 1], waypoints[i]) };
				double speed { waypoints[i].SD.value_or(5.0) };
				if (speed < 0.1)
				{
					speed = 5.0;
				}
				totalDuration += distance / speed;
			}
			return totalDuration;
		}

		double calculateHeadingAngle(const protocol::Waypoint& from, const protocol::Waypoint& to)
		{
			const double deltaLon { (to.JD - from.JD) * M_PI / 180.0 };
			const double fromLatRad { from.WD * M_PI / 180.0 };
			const double toLatRad { to.WD * M_PI / 180.0 };
			const double y { sin(deltaLon) * cos(toLatRad) };
			const double x { cos(fromLatRad) * sin(toLatRad) - sin(fromLatRad) * cos(toLatRad) * cos(deltaLon) };
			const double bearing { atan2(y, x) * 180.0 / M_PI };
			return fmod(bearing + 360.0, 360.0);
		}

		static std::string generateWaylinesKml(const std::vector<protocol::Waypoint>& waypoints)
		{
			std::ostringstream kml {};
			const double	   totalDistance { calculateTotalDistance(waypoints) };
			const double	   totalDuration { calculateTotalDuration(waypoints) };
			const double	   avgSpeed { waypoints.empty() ? 5.0 : waypoints[0].SD.value_or(5.0) };

			kml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
			kml << "<kml xmlns=\"http://www.opengis.net/kml/2.2\" xmlns:wpml=\"http://www.dji.com/wpmz/1.0.6\">\n";
			kml << "  <Document>\n";
			kml << "    <wpml:missionConfig>\n";
			kml << "      <wpml:flyToWaylineMode>safely</wpml:flyToWaylineMode>\n";
			kml << "      <wpml:finishAction>goHome</wpml:finishAction>\n";
			kml << "      <wpml:exitOnRCLost>executeLostAction</wpml:exitOnRCLost>\n";
			kml << "      <wpml:executeRCLostAction>goBack</wpml:executeRCLostAction>\n";
			kml << "      <wpml:takeOffSecurityHeight>20</wpml:takeOffSecurityHeight>\n";
			kml << "      <wpml:globalTransitionalSpeed>" << avgSpeed << "</wpml:globalTransitionalSpeed>\n";
			kml << "      <wpml:droneInfo>\n";
			kml << "        <wpml:droneEnumValue>99</wpml:droneEnumValue>\n";
			kml << "        <wpml:droneSubEnumValue>0</wpml:droneSubEnumValue>\n";
			kml << "      </wpml:droneInfo>\n";
			kml << "      <wpml:waylineAvoidLimitAreaMode>1</wpml:waylineAvoidLimitAreaMode>\n";
			kml << "      <wpml:payloadInfo>\n";
			kml << "        <wpml:payloadEnumValue>89</wpml:payloadEnumValue>\n";
			kml << "        <wpml:payloadSubEnumValue>0</wpml:payloadSubEnumValue>\n";
			kml << "        <wpml:payloadPositionIndex>0</wpml:payloadPositionIndex>\n";
			kml << "      </wpml:payloadInfo>\n";
			kml << "    </wpml:missionConfig>\n";
			kml << "    <Folder>\n";
			kml << "      <wpml:templateId>0</wpml:templateId>\n";
			kml << "      <wpml:executeHeightMode>relativeToStartPoint</wpml:executeHeightMode>\n";
			kml << "      <wpml:waylineId>0</wpml:waylineId>\n";
			kml << "      <wpml:distance>" << std::fixed << std::setprecision(2) << totalDistance << "</wpml:distance>\n";
			kml << "      <wpml:duration>" << std::fixed << std::setprecision(2) << totalDuration << "</wpml:duration>\n";
			kml << "      <wpml:autoFlightSpeed>" << avgSpeed << "</wpml:autoFlightSpeed>\n";

			// 起始动作组
			if (!waypoints.empty())
			{
				kml << "      <wpml:startActionGroup>\n";
				kml << "        <wpml:action>\n";
				kml << "          <wpml:actionId>0</wpml:actionId>\n";
				kml << "          <wpml:actionActuatorFunc>gimbalRotate</wpml:actionActuatorFunc>\n";
				kml << "          <wpml:actionActuatorFuncParam>\n";
				kml << "            <wpml:gimbalHeadingYawBase>aircraft</wpml:gimbalHeadingYawBase>\n";
				kml << "            <wpml:gimbalRotateMode>absoluteAngle</wpml:gimbalRotateMode>\n";
				kml << "            <wpml:gimbalPitchRotateEnable>1</wpml:gimbalPitchRotateEnable>\n";
				kml << "            <wpml:gimbalPitchRotateAngle>" << waypoints[0].YTFYJ.value_or(-90.0) << "</wpml:gimbalPitchRotateAngle>\n";
				kml << "            <wpml:gimbalRollRotateEnable>0</wpml:gimbalRollRotateEnable>\n";
				kml << "            <wpml:gimbalRollRotateAngle>0</wpml:gimbalRollRotateAngle>\n";
				kml << "            <wpml:gimbalYawRotateEnable>1</wpml:gimbalYawRotateEnable>\n";
				kml << "            <wpml:gimbalYawRotateAngle>0</wpml:gimbalYawRotateAngle>\n";
				kml << "            <wpml:gimbalRotateTimeEnable>0</wpml:gimbalRotateTimeEnable>\n";
				kml << "            <wpml:gimbalRotateTime>10</wpml:gimbalRotateTime>\n";
				kml << "            <wpml:payloadPositionIndex>0</wpml:payloadPositionIndex>\n";
				kml << "          </wpml:actionActuatorFuncParam>\n";
				kml << "        </wpml:action>\n";
				kml << "        <wpml:action>\n";
				kml << "          <wpml:actionId>1</wpml:actionId>\n";
				kml << "          <wpml:actionActuatorFunc>hover</wpml:actionActuatorFunc>\n";
				kml << "          <wpml:actionActuatorFuncParam>\n";
				kml << "            <wpml:hoverTime>0.5</wpml:hoverTime>\n";
				kml << "          </wpml:actionActuatorFuncParam>\n";
				kml << "        </wpml:action>\n";
				kml << "      </wpml:startActionGroup>\n";
			}

			// 生成航点
			for (size_t i { 0 }; i < waypoints.size(); ++i)
			{
				const auto& wp { waypoints[i] };
				double		headingAngle { 0.0 };
				if (i < waypoints.size() - 1)
				{
					headingAngle = calculateHeadingAngle(wp, waypoints[i + 1]);
				}

				kml << "      <Placemark>\n";
				kml << "        <Point>\n";
				kml << "          <coordinates>\n";
				kml << "            " << std::fixed << std::setprecision(12) << wp.JD << "," << wp.WD << "\n";
				kml << "          </coordinates>\n";
				kml << "        </Point>\n";
				kml << "        <wpml:index>" << i << "</wpml:index>\n";
				kml << "        <wpml:executeHeight>" << wp.GD << "</wpml:executeHeight>\n";
				kml << "        <wpml:waypointSpeed>" << wp.SD.value_or(5.0) << "</wpml:waypointSpeed>\n";
				kml << "        <wpml:waypointHeadingParam>\n";
				kml << "          <wpml:waypointHeadingMode>followWayline</wpml:waypointHeadingMode>\n";
				kml << "          <wpml:waypointHeadingAngle>" << std::fixed << std::setprecision(6) << headingAngle
					<< "</wpml:waypointHeadingAngle>\n";
				kml << "          <wpml:waypointPoiPoint>0.000000,0.000000,0.000000</wpml:waypointPoiPoint>\n";
				kml << "          <wpml:waypointHeadingAngleEnable>1</wpml:waypointHeadingAngleEnable>\n";
				kml << "          <wpml:waypointHeadingPathMode>followBadArc</wpml:waypointHeadingPathMode>\n";
				kml << "          <wpml:waypointHeadingPoiIndex>0</wpml:waypointHeadingPoiIndex>\n";
				kml << "        </wpml:waypointHeadingParam>\n";
				kml << "        <wpml:waypointTurnParam>\n";

				if ((i == 0) || (i == waypoints.size() - 1))
				{
					kml << "          <wpml:waypointTurnMode>toPointAndStopWithDiscontinuityCurvature</wpml:waypointTurnMode>\n";
					kml << "          <wpml:waypointTurnDampingDist>0</wpml:waypointTurnDampingDist>\n";
				}
				else
				{
					kml << "          <wpml:waypointTurnMode>coordinateTurn</wpml:waypointTurnMode>\n";
					kml << "          <wpml:waypointTurnDampingDist>10</wpml:waypointTurnDampingDist>\n";
				}

				kml << "        </wpml:waypointTurnParam>\n";
				kml << "        <wpml:useStraightLine>1</wpml:useStraightLine>\n";

				// 为第一个航点添加拍照动作
				if (i == 0)
				{
					kml << "        <wpml:actionGroup>\n";
					kml << "          <wpml:actionGroupId>0</wpml:actionGroupId>\n";
					kml << "          <wpml:actionGroupStartIndex>0</wpml:actionGroupStartIndex>\n";
					kml << "          <wpml:actionGroupEndIndex>" << (waypoints.size() - 2) << "</wpml:actionGroupEndIndex>\n";
					kml << "          <wpml:actionGroupMode>sequence</wpml:actionGroupMode>\n";
					kml << "          <wpml:actionTrigger>\n";
					kml << "            <wpml:actionTriggerType>betweenAdjacentPoints</wpml:actionTriggerType>\n";
					kml << "          </wpml:actionTrigger>\n";
					kml << "          <wpml:action>\n";
					kml << "            <wpml:actionId>0</wpml:actionId>\n";
					kml << "            <wpml:actionActuatorFunc>gimbalAngleLock</wpml:actionActuatorFunc>\n";
					kml << "          </wpml:action>\n";
					kml << "          <wpml:action>\n";
					kml << "            <wpml:actionId>1</wpml:actionId>\n";
					kml << "            <wpml:actionActuatorFunc>startTimeLapse</wpml:actionActuatorFunc>\n";
					kml << "            <wpml:actionActuatorFuncParam>\n";
					kml << "              <wpml:payloadPositionIndex>0</wpml:payloadPositionIndex>\n";
					kml << "              <wpml:useGlobalPayloadLensIndex>0</wpml:useGlobalPayloadLensIndex>\n";
					kml << "              <wpml:payloadLensIndex>visable</wpml:payloadLensIndex>\n";
					kml << "              <wpml:minShootInterval>2.0</wpml:minShootInterval>\n";
					kml << "            </wpml:actionActuatorFuncParam>\n";
					kml << "          </wpml:action>\n";
					kml << "        </wpml:actionGroup>\n";
				}

				// 为最后一个航点添加停止拍照动作
				if (i == waypoints.size() - 1)
				{
					kml << "        <wpml:actionGroup>\n";
					kml << "          <wpml:actionGroupId>1</wpml:actionGroupId>\n";
					kml << "          <wpml:actionGroupStartIndex>" << i << "</wpml:actionGroupStartIndex>\n";
					kml << "          <wpml:actionGroupEndIndex>" << i << "</wpml:actionGroupEndIndex>\n";
					kml << "          <wpml:actionGroupMode>sequence</wpml:actionGroupMode>\n";
					kml << "          <wpml:actionTrigger>\n";
					kml << "            <wpml:actionTriggerType>reachPoint</wpml:actionTriggerType>\n";
					kml << "          </wpml:actionTrigger>\n";
					kml << "          <wpml:action>\n";
					kml << "            <wpml:actionId>0</wpml:actionId>\n";
					kml << "            <wpml:actionActuatorFunc>stopTimeLapse</wpml:actionActuatorFunc>\n";
					kml << "            <wpml:actionActuatorFuncParam>\n";
					kml << "              <wpml:payloadPositionIndex>0</wpml:payloadPositionIndex>\n";
					kml << "              <wpml:payloadLensIndex>visable</wpml:payloadLensIndex>\n";
					kml << "            </wpml:actionActuatorFuncParam>\n";
					kml << "          </wpml:action>\n";
					kml << "          <wpml:action>\n";
					kml << "            <wpml:actionId>1</wpml:actionId>\n";
					kml << "            <wpml:actionActuatorFunc>gimbalAngleUnlock</wpml:actionActuatorFunc>\n";
					kml << "          </wpml:action>\n";
					kml << "        </wpml:actionGroup>\n";
				}

				kml << "        <wpml:waypointGimbalHeadingParam>\n";
				kml << "          <wpml:waypointGimbalPitchAngle>0</wpml:waypointGimbalPitchAngle>\n";
				kml << "          <wpml:waypointGimbalYawAngle>0</wpml:waypointGimbalYawAngle>\n";
				kml << "        </wpml:waypointGimbalHeadingParam>\n";
				kml << "        <wpml:isRisky>0</wpml:isRisky>\n";
				kml << "        <wpml:waypointWorkType>0</wpml:waypointWorkType>\n";
				kml << "      </Placemark>\n";
			}

			kml << "    </Folder>\n";
			kml << "  </Document>\n";
			kml << "</kml>\n";

			return kml.str();
		}

		static std::string generateTemplateKml()
		{
			auto			   now { std::chrono::system_clock::now() };
			auto			   time_t_now { std::chrono::system_clock::to_time_t(now) };
			std::tm			   tm_now { *std::localtime(&time_t_now) };
			std::ostringstream kml {};
			kml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
			kml << "<kml xmlns=\"http://www.opengis.net/kml/2.2\" xmlns:wpml=\"http://www.dji.com/wpmz/1.0.6\">\n";
			kml << "  <Document>\n";
			kml << "    <wpml:author>PSDK Application</wpml:author>\n";
			kml << "    <wpml:createTime>" << std::put_time(&tm_now, "%Y-%m-%d %H:%M:%S") << "</wpml:createTime>\n";
			kml << "    <wpml:updateTime>" << std::put_time(&tm_now, "%Y-%m-%d %H:%M:%S") << "</wpml:updateTime>\n";
			kml << "  </Document>\n";
			kml << "</kml>\n";
			return kml.str();
		}
	} // namespace

	bool JsonToKmzConverter::convertWaypointsToKmz(const std::vector<protocol::Waypoint>& waypoints,
												   const protocol::WaypointPayload&		  missionInfo) noexcept
	{
		g_latestKmzFilePath = "";

		const fs::path& storageDir { getKmzStorageDir() };
		if (waypoints.empty() || storageDir.empty())
		{
			LOG_ERROR("无法生成 KMZ：航点列表为空或存储目录无效。");
			return false;
		}

		std::string		  missionId { missionInfo.RWID.value_or("mission_unknown") };
		auto			  now { std::chrono::system_clock::now() };
		auto			  time_t_now { std::chrono::system_clock::to_time_t(now) };
		std::tm			  tm_now { *std::localtime(&time_t_now) };
		std::stringstream time_ss {};
		time_ss << std::put_time(&tm_now, "%Y%m%d_%H%M%S");
		std::string filename { fmt::format("{}_{}.kmz", missionId, time_ss.str()) };
		fs::path	kmzFilePath { storageDir / filename };

		LOG_INFO("开始生成 KMZ 文件，任务 ID: {}, 路径: {}", missionId, kmzFilePath.string());

		std::string waylinesKml { generateWaylinesKml(waypoints) };
		std::string templateKml { generateTemplateKml() };

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

		zip_source_t* waylines_source { zip_source_buffer(archive, waylinesKml.c_str(), waylinesKml.length(), 0) };
		if (!waylines_source)
		{
			LOG_ERROR("");
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
			LOG_ERROR("");
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

		g_latestKmzFilePath = kmzFilePath;

		LOG_INFO("成功创建 KMZ 文件: {}", g_latestKmzFilePath.string());
		return true;
	}

	std::string JsonToKmzConverter::getKmzFilePath(void) noexcept
	{
		return g_latestKmzFilePath.string();
	}
} // namespace plane::utils

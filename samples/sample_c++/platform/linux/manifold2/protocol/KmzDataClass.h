// manifold2/protocol/KmzDataClass.h

#pragma once

#include <chrono>
#include <ctime>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <pugixml.hpp>

#include "define.h"

namespace plane::protocol
{
	namespace kmz
	{
		struct WpmlPoint
		{
			double longitude { 0 };
			double latitude { 0 };

			void   toXml(_PUGI xml_node& parent) const
			{
				auto node { parent.append_child("Point") };
				node.append_child("coordinates").text().set(_FMT format("{:.12f},{:.12f}", longitude, latitude));
			}
		};

		struct WpmlPayloadInfo
		{
			int	 payloadEnumValue { 65'535 };
			int	 payloadSubEnumValue { 0 };
			int	 payloadPositionIndex { 7 };

			void toXml(_PUGI xml_node& parent) const
			{
				auto node { parent.append_child("wpml:payloadInfo") };
				node.append_child("wpml:payloadEnumValue").text().set(payloadEnumValue);
				node.append_child("wpml:payloadSubEnumValue").text().set(payloadSubEnumValue);
				node.append_child("wpml:payloadPositionIndex").text().set(payloadPositionIndex);
			}
		};

		struct PublicKmlMissionConfig
		{
			_STD string		flyToWaylineMode { "safely" };
			_STD string		finishAction { "noAction" };
			_STD string		exitOnRCLost { "goContinue" };
			double			takeOffSecurityHeight { 20.0 };
			double			globalTransitionalSpeed {};
			WpmlPayloadInfo payloadInfo {};

			virtual void	toXml(_PUGI xml_node& parent) const = 0;
		};

		struct PublicWpmlPlacemark
		{
			WpmlPoint	 point {};
			int			 index { 0 };
			int			 isRisky { 0 };
			int			 useStraightLine { 0 };

			virtual void toXml(_PUGI xml_node& parent) const = 0;
		};
	} // namespace kmz

	namespace wpml
	{
		struct WpmlActionActuatorFuncParam
		{
			_STD string gimbalHeadingYawBase { "aircraft" };
			_STD string gimbalRotateMode { "absoluteAngle" };
			int			gimbalPitchRotateEnable { 1 };
			double		gimbalPitchRotateAngle { -90 };
			int			gimbalRollRotateEnable { 0 };
			double		gimbalRollRotateAngle { 0 };
			int			gimbalYawRotateEnable { 0 };
			double		gimbalYawRotateAngle { 0 };
			int			gimbalRotateTimeEnable { 0 };
			int			gimbalRotateTime { 10 };
			int			payloadPositionIndex { 0 };

			void		toXml(_PUGI xml_node& parent) const
			{
				auto node { parent.append_child("wpml:actionActuatorFuncParam") };
				node.append_child("wpml:gimbalHeadingYawBase").text().set(gimbalHeadingYawBase);
				node.append_child("wpml:gimbalRotateMode").text().set(gimbalRotateMode);
				node.append_child("wpml:gimbalPitchRotateEnable").text().set(gimbalPitchRotateEnable);
				node.append_child("wpml:gimbalPitchRotateAngle").text().set(gimbalPitchRotateAngle);
				node.append_child("wpml:gimbalRollRotateEnable").text().set(gimbalRollRotateEnable);
				node.append_child("wpml:gimbalRollRotateAngle").text().set(gimbalRollRotateAngle);
				node.append_child("wpml:gimbalYawRotateEnable").text().set(gimbalYawRotateEnable);
				node.append_child("wpml:gimbalYawRotateAngle").text().set(gimbalYawRotateAngle);
				node.append_child("wpml:gimbalRotateTimeEnable").text().set(gimbalRotateTimeEnable);
				node.append_child("wpml:gimbalRotateTime").text().set(gimbalRotateTime);
				node.append_child("wpml:payloadPositionIndex").text().set(payloadPositionIndex);
			}
		};

		struct WpmlAction
		{
			int							actionId { 0 };
			_STD string					actionActuatorFunc {};
			WpmlActionActuatorFuncParam actionActuatorFuncParam {};

			void						toXml(_PUGI xml_node& parent) const
			{
				auto node { parent.append_child("wpml:action") };
				node.append_child("wpml:actionId").text().set(actionId);
				node.append_child("wpml:actionActuatorFunc").text().set(actionActuatorFunc);
				actionActuatorFuncParam.toXml(node);
			}
		};

		struct WpmlActionGroup
		{
			int			actionGroupId { 0 };
			int			actionGroupStartIndex { 1 };
			int			actionGroupEndIndex { 1 };
			_STD string actionGroupMode { "sequence" };
			_STD string actionTriggerType {};
			_STD vector<WpmlAction> actions {};

			void					toXml(_PUGI xml_node& parent) const
			{
				auto node { parent.append_child("wpml:actionGroup") };
				node.append_child("wpml:actionGroupId").text().set(actionGroupId);
				node.append_child("wpml:actionGroupStartIndex").text().set(actionGroupStartIndex);
				node.append_child("wpml:actionGroupEndIndex").text().set(actionGroupEndIndex);
				node.append_child("wpml:actionGroupMode").text().set(actionGroupMode);

				auto triggerNode { node.append_child("wpml:actionTrigger") };
				triggerNode.append_child("wpml:actionTriggerType").text().set(actionTriggerType);

				for (const auto& action : actions)
				{
					action.toXml(node);
				}
			}
		};

		struct WpmlWaypointHeadingParam
		{
			_STD string waypointHeadingMode { "followWayline" };
			double		waypointHeadingAngle { 0 };
			_STD string waypointPoiPoint { _FMT format("{:.6f},{:.6f},{:.6f}", .0, .0, .0) };
			int			waypointHeadingAngleEnable { 0 };
			int			waypointHeadingPoiIndex { 0 };

			void		toXml(_PUGI xml_node& parent) const
			{
				auto node { parent.append_child("wpml:waypointHeadingParam") };
				node.append_child("wpml:waypointHeadingMode").text().set(waypointHeadingMode);
				node.append_child("wpml:waypointHeadingAngle").text().set(waypointHeadingAngle);
				node.append_child("wpml:waypointPoiPoint").text().set(waypointPoiPoint);
				node.append_child("wpml:waypointHeadingAngleEnable").text().set(waypointHeadingAngleEnable);
				node.append_child("wpml:waypointHeadingPoiIndex").text().set(waypointHeadingPoiIndex);
			}
		};

		struct WpmlWaypointTurnParam
		{
			_STD string waypointTurnMode { "toPointAndStopWithContinuityCurvature" };
			double		waypointTurnDampingDist { 0 };

			void		toXml(_PUGI xml_node& parent) const
			{
				auto node { parent.append_child("wpml:waypointTurnParam") };
				node.append_child("wpml:waypointTurnMode").text().set(waypointTurnMode);
				node.append_child("wpml:waypointTurnDampingDist").text().set(waypointTurnDampingDist);
			}
		};

		struct WpmlGimbalHeadingParam
		{
			double waypointGimbalPitchAngle { 0 };
			double waypointGimbalYawAngle { 0 };

			void   toXml(_PUGI xml_node& parent) const
			{
				auto node { parent.append_child("wpml:waypointGimbalHeadingParam") };
				node.append_child("wpml:waypointGimbalPitchAngle").text().set(waypointGimbalPitchAngle);
				node.append_child("wpml:waypointGimbalYawAngle").text().set(waypointGimbalYawAngle);
			}
		};

		struct WpmlPlacemark final: public kmz::PublicWpmlPlacemark
		{
			double					 executeHeight { 40 };
			double					 waypointSpeed { 5 };
			WpmlWaypointHeadingParam waypointHeadingParam {};
			WpmlWaypointTurnParam	 waypointTurnParam {};
			_STD vector<WpmlActionGroup> actionGroups {};
			WpmlGimbalHeadingParam		 waypointGimbalHeadingParam {};
			int							 waypointWorkType { 0 };

			void						 toXml(_PUGI xml_node& parent) const
			{
				auto node { parent.append_child("Placemark") };
				point.toXml(node);
				node.append_child("wpml:index").text().set(index);
				node.append_child("wpml:executeHeight").text().set(executeHeight);
				node.append_child("wpml:waypointSpeed").text().set(waypointSpeed);
				waypointHeadingParam.toXml(node);
				waypointTurnParam.toXml(node);
				node.append_child("wpml:useStraightLine").text().set(useStraightLine);
				for (const auto& ag : actionGroups)
				{
					ag.toXml(node);
				}
				waypointGimbalHeadingParam.toXml(node);
				node.append_child("wpml:isRisky").text().set(isRisky);
				node.append_child("wpml:waypointWorkType").text().set(waypointWorkType);
			}
		};

		struct WpmlFolder
		{
			int			templateId { 0 };
			_STD string executeHeightMode { "relativeToStartPoint" };
			int			waylineId { 0 };
			double		distance { 530.481018066406 };
			double		duration { 149.069183349609 };
			double		autoFlightSpeed { 5 };
			_STD vector<WpmlPlacemark> placemarks {};

			void					   toXml(_PUGI xml_node& parent) const
			{
				auto node { parent.append_child("Folder") };
				node.append_child("wpml:templateId").text().set(templateId);
				node.append_child("wpml:executeHeightMode").text().set(executeHeightMode);
				node.append_child("wpml:waylineId").text().set(waylineId);
				node.append_child("wpml:distance").text().set(_FMT format("{:.12f}", distance));
				node.append_child("wpml:duration").text().set(_FMT format("{:.12f}", duration));
				node.append_child("wpml:autoFlightSpeed").text().set(autoFlightSpeed);
				for (const auto& pm : placemarks)
				{
					pm.toXml(node);
				}
			}
		};

		struct WpmlPayloadInfo
		{
			int	 payloadEnumValue { 65'535 };
			int	 payloadSubEnumValue { 0 };
			int	 payloadPositionIndex { 0 };

			void toXml(_PUGI xml_node& parent) const
			{
				auto node { parent.append_child("wpml:payloadInfo") };
				node.append_child("wpml:payloadEnumValue").text().set(payloadEnumValue);
				node.append_child("wpml:payloadSubEnumValue").text().set(payloadSubEnumValue);
				node.append_child("wpml:payloadPositionIndex").text().set(payloadPositionIndex);
			}
		};

		struct WpmlDroneInfo
		{
			int	 droneEnumValue { 65'535 };
			int	 droneSubEnumValue { 0 };

			void toXml(_PUGI xml_node& parent) const
			{
				auto node { parent.append_child("wpml:droneInfo") };
				node.append_child("wpml:droneEnumValue").text().set(droneEnumValue);
				node.append_child("wpml:droneSubEnumValue").text().set(droneSubEnumValue);
			}
		};

		struct KmlMissionConfig final: public kmz::PublicKmlMissionConfig
		{
			_STD string	  executeRCLostAction { "goBack" };
			WpmlDroneInfo droneInfo {};

			void		  toXml(_PUGI xml_node& parent) const
			{
				auto node { parent.append_child("wpml:missionConfig") };
				node.append_child("wpml:flyToWaylineMode").text().set(flyToWaylineMode);
				node.append_child("wpml:finishAction").text().set(finishAction);
				node.append_child("wpml:exitOnRCLost").text().set(exitOnRCLost);
				node.append_child("wpml:takeOffSecurityHeight").text().set(takeOffSecurityHeight);
				node.append_child("wpml:globalTransitionalSpeed").text().set(globalTransitionalSpeed);

				droneInfo.toXml(node);
				payloadInfo.toXml(node);
			}
		};

		struct WpmlDocument
		{
			KmlMissionConfig missionConfig {};
			WpmlFolder		 folder {};

			void			 toXml(_PUGI xml_node& parent) const
			{
				missionConfig.toXml(parent);
				folder.toXml(parent);
			}
		};

		struct WaylinesWpmlFile
		{
			WpmlDocument document {};

			void		 toXml(_PUGI xml_document& doc) const noexcept
			{
				auto decl { doc.append_child(_PUGI node_declaration) };
				decl.append_attribute("version")  = "1.0";
				decl.append_attribute("encoding") = "UTF-8";

				auto kmlNode { doc.append_child("kml") };
				kmlNode.append_attribute("xmlns")	   = "http://www.opengis.net/kml/2.2";
				kmlNode.append_attribute("xmlns:wpml") = "http://www.dji.com/wpmz/1.0.6";

				auto docNode { kmlNode.append_child("Document") };
				document.toXml(docNode);
			}
		};
	} // namespace wpml

	namespace kml
	{
		struct WpmlCoordinateSysParam
		{
			_STD string coordinateMode { "WGS84" };
			_STD string heightMode { "relativeToStartPoint" };
			_STD string positioningType { "GPS" };

			void		toXml(_PUGI xml_node& parent) const
			{
				auto node { parent.append_child("wpml:waylineCoordinateSysParam") };
				node.append_child("wpml:coordinateMode").text().set(coordinateMode);
				node.append_child("wpml:heightMode").text().set(heightMode);
				node.append_child("wpml:positioningType").text().set(positioningType);
			}
		};

		struct WpmlGlobalWaypointHeadingParam
		{
			_STD string waypointHeadingMode { "followWayline" };
			double		waypointHeadingAngle { 0 };
			_STD string waypointPoiPoint { _FMT format("{:.6f},{:.6f},{:.6f}", .0, .0, .0) };
			int			waypointHeadingPoiIndex { 0 };

			void		toXml(_PUGI xml_node& parent) const
			{
				auto node { parent.append_child("wpml:globalWaypointHeadingParam") };
				node.append_child("wpml:waypointHeadingMode").text().set(waypointHeadingMode);
				node.append_child("wpml:waypointHeadingAngle").text().set(waypointHeadingAngle);
				node.append_child("wpml:waypointPoiPoint").text().set(waypointPoiPoint);
				node.append_child("wpml:waypointHeadingPoiIndex").text().set(waypointHeadingPoiIndex);
			}
		};

		struct WpmlPayloadParam
		{
			int	 payloadPositionIndex { 7 };

			void toXml(_PUGI xml_node& parent) const
			{
				auto node { parent.append_child("wpml:payloadParam") };
				node.append_child("wpml:payloadPositionIndex").text().set(payloadPositionIndex);
			}
		};

		struct WpmlPlacemark final: public kmz::PublicWpmlPlacemark
		{
			double height { 100.0 };
			double ellipsoidHeight { 100.0 };
			int	   useGlobalHeight { 1 };
			int	   useGlobalSpeed { 1 };
			int	   useGlobalHeadingParam { 1 };
			int	   useGlobalTurnParam { 1 };

			void   toXml(_PUGI xml_node& parent) const
			{
				auto node { parent.append_child("Placemark") };
				point.toXml(node);
				node.append_child("wpml:index").text().set(index);
				node.append_child("wpml:ellipsoidHeight").text().set(ellipsoidHeight);
				node.append_child("wpml:height").text().set(height);
				node.append_child("wpml:useGlobalHeight").text().set(useGlobalHeight);
				node.append_child("wpml:useGlobalSpeed").text().set(useGlobalSpeed);
				node.append_child("wpml:useGlobalHeadingParam").text().set(useGlobalHeadingParam);
				node.append_child("wpml:useGlobalTurnParam").text().set(useGlobalTurnParam);
				node.append_child("wpml:useStraightLine").text().set(useStraightLine);
				node.append_child("wpml:isRisky").text().set(isRisky);
			}
		};

		struct WpmlFolder
		{
			_STD string					   templateType { "waypoint" };
			int							   templateId { 0 };
			WpmlCoordinateSysParam		   coordinateSysParam {};
			double						   autoFlightSpeed { 10.0 };
			double						   globalHeight { 100.0 };
			int							   caliFlightEnable { 0 };
			_STD string					   gimbalPitchMode { "manual" };
			WpmlGlobalWaypointHeadingParam globalWaypointHeadingParam {};
			_STD string					   globalWaypointTurnMode { "toPointAndPassWithContinuityCurvature" };
			int							   globalUseStraightLine { 1 };
			WpmlPayloadParam			   payloadParam {};
			_STD vector<WpmlPlacemark> placemarks {};

			void					   toXml(_PUGI xml_node& parent) const
			{
				auto node { parent.append_child("Folder") };
				node.append_child("wpml:templateType").text().set(templateType);
				node.append_child("wpml:templateId").text().set(templateId);
				coordinateSysParam.toXml(node);
				node.append_child("wpml:autoFlightSpeed").text().set(autoFlightSpeed);
				node.append_child("wpml:globalHeight").text().set(globalHeight);
				node.append_child("wpml:caliFlightEnable").text().set(caliFlightEnable);
				node.append_child("wpml:gimbalPitchMode").text().set(gimbalPitchMode);
				globalWaypointHeadingParam.toXml(node);
				node.append_child("wpml:globalWaypointTurnMode").text().set(globalWaypointTurnMode);
				node.append_child("wpml:globalUseStraightLine").text().set(globalUseStraightLine);
				for (const auto& pm : placemarks)
				{
					pm.toXml(node);
				}
				payloadParam.toXml(node);
			}
		};

		struct WpmlDroneInfo
		{
			int	 droneEnumValue { 78 };
			int	 droneSubEnumValue { 0 };

			void toXml(_PUGI xml_node& parent) const
			{
				auto node { parent.append_child("wpml:droneInfo") };
				node.append_child("wpml:droneEnumValue").text().set(droneEnumValue);
				node.append_child("wpml:droneSubEnumValue").text().set(droneSubEnumValue);
			}
		};

		struct WpmlPayloadInfo
		{
			int	 payloadEnumValue { 65'535 };
			int	 payloadSubEnumValue { 0 };
			int	 payloadPositionIndex { 7 };

			void toXml(_PUGI xml_node& parent) const
			{
				auto node { parent.append_child("wpml:payloadInfo") };
				node.append_child("wpml:payloadEnumValue").text().set(payloadEnumValue);
				node.append_child("wpml:payloadSubEnumValue").text().set(payloadSubEnumValue);
				node.append_child("wpml:payloadPositionIndex").text().set(payloadPositionIndex);
			}
		};

		struct KmlMissionConfig final: public kmz::PublicKmlMissionConfig
		{
			WpmlDroneInfo droneInfo {};

			void		  toXml(_PUGI xml_node& parent) const
			{
				auto node { parent.append_child("wpml:missionConfig") };
				node.append_child("wpml:flyToWaylineMode").text().set(flyToWaylineMode);
				node.append_child("wpml:finishAction").text().set(finishAction);
				node.append_child("wpml:exitOnRCLost").text().set(exitOnRCLost);
				node.append_child("wpml:takeOffSecurityHeight").text().set(takeOffSecurityHeight);
				node.append_child("wpml:globalTransitionalSpeed").text().set(globalTransitionalSpeed);
				droneInfo.toXml(node);
				payloadInfo.toXml(node);
			}
		};

		struct WpmlDocument
		{
			KmlMissionConfig missionConfig {};
			WpmlFolder		 folder {};

			void			 toXml(_PUGI xml_node& parent) const
			{
				missionConfig.toXml(parent);
				folder.toXml(parent);
			}
		};

		struct TemplateKmlFile
		{
			_STD string	 createTime {};
			_STD string	 updateTime {};
			WpmlDocument document {};

			explicit TemplateKmlFile(void) noexcept
			{
				auto now_ms { _STD_CHRONO duration_cast<_STD_CHRONO milliseconds>(_STD_CHRONO system_clock::now().time_since_epoch()).count() };
				createTime = _STD to_string(now_ms);
				updateTime = createTime;
			}

			void toXml(_PUGI xml_document& doc) const noexcept
			{
				auto decl { doc.append_child(_PUGI node_declaration) };
				decl.append_attribute("version")  = "1.0";
				decl.append_attribute("encoding") = "UTF-8";

				auto kmlNode { doc.append_child("kml") };
				kmlNode.append_attribute("xmlns")	   = "http://www.opengis.net/kml/2.2";
				kmlNode.append_attribute("xmlns:wpml") = "http://www.dji.com/wpmz/1.0.6";

				auto docNode { kmlNode.append_child("Document") };
				docNode.append_child("wpml:createTime").text().set(createTime);
				docNode.append_child("wpml:updateTime").text().set(updateTime);

				document.toXml(docNode);
			}
		};
	} // namespace kml
} // namespace plane::protocol

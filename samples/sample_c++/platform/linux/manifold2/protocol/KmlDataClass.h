#pragma once

#include "fmt/format.h"
#include "pugixml.hpp"

#include <optional>
#include <string>
#include <vector>

namespace plane::protocol
{
	struct KmlPoint
	{
		double longitude { 0.0 };
		double latitude { 0.0 };

		void   toXml(pugi::xml_node& parent) const
		{
			auto pointNode { parent.append_child("Point") };
			pointNode.append_child("coordinates").text().set(fmt::format("{:.12f},{:.12f}", longitude, latitude));
		}
	};

	struct KmlWaypointHeadingParam
	{
		std::string headingMode { "followWayline" };
		double		headingAngle { 0.0 };
		std::string poiPoint { "0.000000,0.000000,0.000000" };
		int			headingAngleEnable { 1 };
		std::string headingPathMode { "followBadArc" };
		int			headingPoiIndex { 0 };

		void		toXml(pugi::xml_node& parent) const
		{
			auto node { parent.append_child("wpml:waypointHeadingParam") };
			node.append_child("wpml:waypointHeadingMode").text().set(headingMode);
			node.append_child("wpml:waypointHeadingAngle").text().set(headingAngle);
			node.append_child("wpml:waypointPoiPoint").text().set(poiPoint);
			node.append_child("wpml:waypointHeadingAngleEnable").text().set(headingAngleEnable);
			node.append_child("wpml:waypointHeadingPathMode").text().set(headingPathMode);
			node.append_child("wpml:waypointHeadingPoiIndex").text().set(headingPoiIndex);
		}
	};

	struct KmlWaypointTurnParam
	{
		std::string turnMode {};
		int			turnDampingDist { 0 };

		void		toXml(pugi::xml_node& parent) const
		{
			auto node { parent.append_child("wpml:waypointTurnParam") };
			node.append_child("wpml:waypointTurnMode").text().set(turnMode);
			node.append_child("wpml:waypointTurnDampingDist").text().set(turnDampingDist);
		}
	};

	struct KmlGimbalHeadingParam
	{
		double gimbalPitchAngle { 0.0 };
		double gimbalYawAngle { 0.0 };

		void   toXml(pugi::xml_node& parent) const
		{
			auto node { parent.append_child("wpml:waypointGimbalHeadingParam") };
			node.append_child("wpml:waypointGimbalPitchAngle").text().set(gimbalPitchAngle);
			node.append_child("wpml:waypointGimbalYawAngle").text().set(gimbalYawAngle);
		}
	};

	struct KmlActionActuatorFuncParam
	{
		std::optional<double>	   gimbalPitchRotateAngle {};
		std::optional<double>	   hoverTime {};
		std::optional<int>		   payloadPositionIndex {};
		std::optional<int>		   useGlobalPayloadLensIndex {};
		std::optional<std::string> payloadLensIndex {};
		std::optional<double>	   minShootInterval {};
		std::optional<std::string> gimbalHeadingYawBase {};
		std::optional<std::string> gimbalRotateMode {};
		std::optional<int>		   gimbalPitchRotateEnable {};
		std::optional<int>		   gimbalRollRotateEnable {};
		std::optional<double>	   gimbalRollRotateAngle {};
		std::optional<int>		   gimbalYawRotateEnable {};
		std::optional<double>	   gimbalYawRotateAngle {};
		std::optional<int>		   gimbalRotateTimeEnable {};
		std::optional<int>		   gimbalRotateTime {};

		void					   toXml(pugi::xml_node& parent) const
		{
			auto node { parent.append_child("wpml:actionActuatorFuncParam") };
			if (gimbalHeadingYawBase)
			{
				node.append_child("wpml:gimbalHeadingYawBase").text().set(gimbalHeadingYawBase.value_or(""));
			}
			if (gimbalRotateMode)
			{
				node.append_child("wpml:gimbalRotateMode").text().set(gimbalRotateMode.value_or(""));
			}
			if (gimbalPitchRotateEnable)
			{
				node.append_child("wpml:gimbalPitchRotateEnable").text().set(*gimbalPitchRotateEnable);
			}
			if (gimbalPitchRotateAngle)
			{
				node.append_child("wpml:gimbalPitchRotateAngle").text().set(fmt::format("{:.2f}", *gimbalPitchRotateAngle));
			}
			if (gimbalRollRotateEnable)
			{
				node.append_child("wpml:gimbalRollRotateEnable").text().set(*gimbalRollRotateEnable);
			}
			if (gimbalRollRotateAngle)
			{
				node.append_child("wpml:gimbalRollRotateAngle").text().set(fmt::format("{:.2f}", *gimbalRollRotateAngle));
			}
			if (gimbalYawRotateEnable)
			{
				node.append_child("wpml:gimbalYawRotateEnable").text().set(*gimbalYawRotateEnable);
			}
			if (gimbalYawRotateAngle)
			{
				node.append_child("wpml:gimbalYawRotateAngle").text().set(fmt::format("{:.2f}", *gimbalYawRotateAngle));
			}
			if (gimbalRotateTimeEnable)
			{
				node.append_child("wpml:gimbalRotateTimeEnable").text().set(*gimbalRotateTimeEnable);
			}
			if (gimbalRotateTime)
			{
				node.append_child("wpml:gimbalRotateTime").text().set(*gimbalRotateTime);
			}
			if (hoverTime)
			{
				node.append_child("wpml:hoverTime").text().set(fmt::format("{:.1f}", *hoverTime));
			}
			if (payloadPositionIndex)
			{
				node.append_child("wpml:payloadPositionIndex").text().set(*payloadPositionIndex);
			}
			if (useGlobalPayloadLensIndex)
			{
				node.append_child("wpml:useGlobalPayloadLensIndex").text().set(*useGlobalPayloadLensIndex);
			}
			if (payloadLensIndex)
			{
				node.append_child("wpml:payloadLensIndex").text().set(payloadLensIndex.value_or(""));
			}
			if (minShootInterval)
			{
				node.append_child("wpml:minShootInterval").text().set(fmt::format("{:.1f}", *minShootInterval));
			}
		}
	};

	struct KmlAction
	{
		int										  actionId { 0 };
		std::string								  actuatorFunc {};
		std::optional<KmlActionActuatorFuncParam> actuatorFuncParam {};

		void									  toXml(pugi::xml_node& parent) const
		{
			auto node { parent.append_child("wpml:action") };
			node.append_child("wpml:actionId").text().set(actionId);
			node.append_child("wpml:actionActuatorFunc").text().set(actuatorFunc);
			if (actuatorFuncParam)
			{
				actuatorFuncParam->toXml(node);
			}
		}
	};

	struct KmlActionGroup
	{
		int					   groupId { 0 };
		int					   startIndex { 0 };
		int					   endIndex { 0 };
		std::string			   mode { "sequence" };
		std::string			   triggerType {};
		std::vector<KmlAction> actions {};

		void				   toXml(pugi::xml_node& parent, bool isStartActionGroup = false) const
		{
			auto node = parent.append_child(isStartActionGroup ? "wpml:startActionGroup" : "wpml:actionGroup");
			if (!isStartActionGroup)
			{
				node.append_child("wpml:actionGroupId").text().set(groupId);
				node.append_child("wpml:actionGroupStartIndex").text().set(startIndex);
				node.append_child("wpml:actionGroupEndIndex").text().set(endIndex);
				node.append_child("wpml:actionGroupMode").text().set(mode);
				auto triggerNode = node.append_child("wpml:actionTrigger");
				triggerNode.append_child("wpml:actionTriggerType").text().set(triggerType);
			}
			for (const auto& action : actions)
			{
				action.toXml(node);
			}
		}
	};

	struct KmlPlacemark
	{
		KmlPoint					point {};
		int							index { 0 };
		double						executeHeight { 0.0 };
		double						waypointSpeed { 5.0 };
		KmlWaypointHeadingParam		headingParam {};
		KmlWaypointTurnParam		turnParam {};
		int							useStraightLine { 1 };
		std::vector<KmlActionGroup> actionGroups {};
		KmlGimbalHeadingParam		gimbalHeadingParam {};
		int							isRisky { 0 };
		int							workType { 0 };

		void						toXml(pugi::xml_node& parent) const
		{
			auto node { parent.append_child("Placemark") };
			point.toXml(node);
			node.append_child("wpml:index").text().set(index);
			node.append_child("wpml:executeHeight").text().set(fmt::format("{:.12f}", executeHeight));
			node.append_child("wpml:waypointSpeed").text().set(fmt::format("{:.12f}", waypointSpeed));
			headingParam.toXml(node);
			turnParam.toXml(node);
			node.append_child("wpml:useStraightLine").text().set(useStraightLine);
			for (const auto& ag : actionGroups)
			{
				ag.toXml(node);
			}
			gimbalHeadingParam.toXml(node);
			node.append_child("wpml:isRisky").text().set(isRisky);
			node.append_child("wpml:waypointWorkType").text().set(workType);
		}
	};

	struct KmlFolder
	{
		int							templateId { 0 };
		std::string					executeHeightMode { "relativeToStartPoint" };
		int							waylineId { 0 };
		double						distance { 0.0 };
		double						duration { 0.0 };
		double						autoFlightSpeed { 5.0 };
		std::vector<KmlActionGroup> startActionGroups;
		std::vector<KmlPlacemark>	placemarks;

		void						toXml(pugi::xml_node& parent) const
		{
			auto node { parent.append_child("Folder") };
			node.append_child("wpml:templateId").text().set(templateId);
			node.append_child("wpml:executeHeightMode").text().set(executeHeightMode);
			node.append_child("wpml:waylineId").text().set(waylineId);
			node.append_child("wpml:distance").text().set(fmt::format("{:.2f}", distance));
			node.append_child("wpml:duration").text().set(fmt::format("{:.2f}", duration));
			node.append_child("wpml:autoFlightSpeed").text().set(fmt::format("{:.2f}", autoFlightSpeed));
			for (const auto& ag : startActionGroups)
			{
				ag.toXml(node);
			}
			for (const auto& pm : placemarks)
			{
				pm.toXml(node);
			}
		}
	};

	struct KmlMissionConfig
	{
		std::string flyToWaylineMode { "safely" };
		std::string finishAction { "goHome" };
		std::string exitOnRCLost { "executeLostAction" };
		std::string executeRCLostAction { "goBack" };
		int			takeOffSecurityHeight { 20 };
		double		globalTransitionalSpeed { 5.0 };

		struct DroneInfo
		{
			int	 droneEnumValue { 99 };
			int	 droneSubEnumValue { 0 };

			void toXml(pugi::xml_node& parent) const
			{
				auto node { parent.append_child("wpml:droneInfo") };
				node.append_child("wpml:droneEnumValue").text().set(droneEnumValue);
				node.append_child("wpml:droneSubEnumValue").text().set(droneSubEnumValue);
			}
		} droneInfo;

		int waylineAvoidLimitAreaMode { 1 };

		struct PayloadInfo
		{
			int	 payloadEnumValue { 89 };
			int	 payloadSubEnumValue { 0 };
			int	 payloadPositionIndex { 0 };

			void toXml(pugi::xml_node& parent) const
			{
				auto node { parent.append_child("wpml:payloadInfo") };
				node.append_child("wpml:payloadEnumValue").text().set(payloadEnumValue);
				node.append_child("wpml:payloadSubEnumValue").text().set(payloadSubEnumValue);
				node.append_child("wpml:payloadPositionIndex").text().set(payloadPositionIndex);
			}
		} payloadInfo;

		void toXml(pugi::xml_node& parent) const
		{
			auto node { parent.append_child("wpml:missionConfig") };
			node.append_child("wpml:flyToWaylineMode").text().set(flyToWaylineMode);
			node.append_child("wpml:finishAction").text().set(finishAction);
			node.append_child("wpml:exitOnRCLost").text().set(exitOnRCLost);
			node.append_child("wpml:executeRCLostAction").text().set(executeRCLostAction);
			node.append_child("wpml:takeOffSecurityHeight").text().set(takeOffSecurityHeight);
			node.append_child("wpml:globalTransitionalSpeed").text().set(fmt::format("{:.2f}", globalTransitionalSpeed));

			droneInfo.toXml(node);

			node.append_child("wpml:waylineAvoidLimitAreaMode").text().set(waylineAvoidLimitAreaMode);

			payloadInfo.toXml(node);
		}
	};

	struct KmlDocument
	{
		KmlMissionConfig missionConfig {};
		KmlFolder		 folder {};

		void			 toXml(pugi::xml_node& parent) const
		{
			auto node { parent.append_child("Document") };
			missionConfig.toXml(node);
			folder.toXml(node);
		}
	};

	struct KmlRoot
	{
		std::string xml_version { "1.0" };
		std::string encoding { "UTF-8" };
		std::string xmlns { "http://www.opengis.net/kml/2.2" };
		std::string xmlns_wpml { "http://www.dji.com/wpmz/1.0.6" };
		KmlDocument document {};

		void		toXml(pugi::xml_document& doc) const
		{
			auto decl { doc.append_child(pugi::node_declaration) };
			decl.append_attribute("version")  = xml_version;
			decl.append_attribute("encoding") = encoding;

			auto kmlNode { doc.append_child("kml") };
			kmlNode.append_attribute("xmlns")	   = xmlns;
			kmlNode.append_attribute("xmlns:wpml") = xmlns_wpml;

			document.toXml(kmlNode);
		}
	};
} // namespace plane::protocol

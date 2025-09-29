// manifold2/utils/XmlUtils.h

#pragma once

#include <sstream>
#include <string>

#include <pugixml.hpp>

#include "protocol/KmzDataClass.h"

#include "define.h"

namespace plane::utils
{
	inline _STD string toXmlString(const plane::protocol::wpml::WaylinesWpmlFile& root)
	{
		_PUGI xml_document doc {};
		root.toXml(doc);
		_STD ostringstream oss {};
		doc.save(oss, "  ", _PUGI format_default, _PUGI encoding_utf8);
		return oss.str();
	}

	inline _STD string toXmlString(const plane::protocol::kml::TemplateKmlFile& tpl)
	{
		_PUGI xml_document doc {};
		tpl.toXml(doc);
		_STD ostringstream oss {};
		doc.save(oss, "  ", _PUGI format_default, _PUGI encoding_utf8);
		return oss.str();
	}
} // namespace plane::utils

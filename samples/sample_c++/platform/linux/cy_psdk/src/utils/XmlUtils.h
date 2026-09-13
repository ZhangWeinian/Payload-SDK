// cy_psdk/utils/XmlUtils.h

#pragma once

#include "protocol/KmzDataClass.h"

#include <pugixml.hpp>

#include <sstream>
#include <string>

#include "define.h"

namespace plane::utils
{
    inline ::std::string toXmlString(const plane::protocol::wpml::WaylinesWpmlFile& root)
    {
        ::pugi::xml_document doc {};
        root.toXml(doc);
        ::std::ostringstream oss {};
        doc.save(oss, "  ", ::pugi::format_default, ::pugi::encoding_utf8);
        return oss.str();
    }

    inline ::std::string toXmlString(const plane::protocol::kml::TemplateKmlFile& tpl)
    {
        ::pugi::xml_document doc {};
        tpl.toXml(doc);
        ::std::ostringstream oss {};
        doc.save(oss, "  ", ::pugi::format_default, ::pugi::encoding_utf8);
        return oss.str();
    }
} // namespace plane::utils

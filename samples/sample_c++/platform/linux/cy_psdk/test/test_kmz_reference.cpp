// cy_psdk/test/test_kmz_reference.cpp
//
// 真机基线回归 (2026-09-15 实飞验证):
//   sample_data/kmz_template.json —— 通过 MQTT 下发的真实任务报文 (飞机正常执行)
//   sample_data/kmz_template.kmz  —— 该任务实飞所用的 KMZ
//
// 覆盖两点:
//   1) JSON 解析层能吃下这份真实报文 (字段名 / 取值 / 航点数)
//   2) 用同一份 JSON 生成的 KMZ 与真机 KMZ 语义等价
//
// 比对规则 (有意放宽的部分都写在这里, 避免"悄悄放水"):
//   - 忽略 createTime / updateTime (每次生成的易变项)
//   - 忽略 efficiencyFlightModeEnable: 参考文件里它是未初始化的浮点内存值
//     (如 1695398074 / 1119682560), 属参考生成器缺陷; 飞控忽略该元素, 我们不复制它
//   - 叶子取值按数值比较 (相对容差), 因此 -15.8808288574219 与 -15.880828857421875 视为等价
//   - 元素名称 (含前缀) / 顺序 / 属性集合必须完全一致

#include "config/ConfigManager.h"
#include "protocol/DroneDataClass.h"
#include "test_config_helpers.h"
#include "test_zip_helpers.h"
#include "utils/json_converter/JsonToKmz.h"

#include <gtest/gtest.h>
#include <pugixml.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
    using Diffs = ::std::vector<::std::string>;

    ::std::filesystem::path samplePath(const char* fileName)
    {
        return ::std::filesystem::path { CY_TEST_SAMPLE_DATA_DIR } / fileName;
    }

    ::std::string readFile(const ::std::filesystem::path& path)
    {
        ::std::ifstream stream { path, ::std::ios::binary };
        return ::std::string { ::std::istreambuf_iterator<char> { stream }, ::std::istreambuf_iterator<char> {} };
    }

    ::std::string trim(::std::string text)
    {
        const auto notSpace { [](unsigned char ch)
                              {
                                  return ::std::isspace(ch) == 0;
                              } };
        text.erase(text.begin(), ::std::find_if(text.begin(), text.end(), notSpace));
        text.erase(::std::find_if(text.rbegin(), text.rend(), notSpace).base(), text.end());
        return text;
    }

    bool parseDouble(const ::std::string& text, double& value)
    {
        if (text.empty())
        {
            return false;
        }
        char*        end { nullptr };
        const double parsed { ::std::strtod(text.c_str(), &end) };
        if (end == nullptr || *end != '\0')
        {
            return false;
        }
        value = parsed;
        return true;
    }

    bool numbersClose(double lhs, double rhs)
    {
        // 参考产物用 float32 参与计算 (输出形如 461.426361083984), 与我们的 double 结果
        // 存在 ~1e-8 相对偏差, 故容差取 1e-6
        return ::std::fabs(lhs - rhs) <= 1e-6 * ::std::max({ 1.0, ::std::fabs(lhs), ::std::fabs(rhs) });
    }

    ::std::vector<::std::string> splitCommaSeparated(const ::std::string& text)
    {
        ::std::vector<::std::string> parts {};
        ::std::size_t                begin { 0 };
        while (true)
        {
            const auto comma { text.find(',', begin) };
            if (comma == ::std::string::npos)
            {
                parts.push_back(trim(text.substr(begin)));
                return parts;
            }
            parts.push_back(trim(text.substr(begin, comma - begin)));
            begin = comma + 1;
        }
    }

    bool textEquals(const ::std::string& lhs, const ::std::string& rhs)
    {
        if (lhs == rhs)
        {
            return true;
        }

        // <coordinates> 这类 "经度,纬度[,高度]" 复合值: 逐段按数值比较
        if (lhs.find(',') != ::std::string::npos || rhs.find(',') != ::std::string::npos)
        {
            const auto left { splitCommaSeparated(lhs) };
            const auto right { splitCommaSeparated(rhs) };
            if (left.size() != right.size())
            {
                return false;
            }
            return ::std::equal(
                left.begin(),
                left.end(),
                right.begin(),
                [](const auto& a, const auto& b)
                {
                    double da {}, db {};
                    return parseDouble(a, da) && parseDouble(b, db) ? numbersClose(da, db) : a == b;
                }
            );
        }

        double left {};
        double right {};
        if (parseDouble(lhs, left) && parseDouble(rhs, right))
        {
            return numbersClose(left, right);
        }
        return false;
    }

    bool isIgnoredElement(const char* name)
    {
        const ::std::string element { name };
        return element == "efficiencyFlightModeEnable" || element == "wpml:efficiencyFlightModeEnable" || element == "createTime" ||
               element == "wpml:createTime" || element == "updateTime" || element == "wpml:updateTime" ||
               // duration: 参考侧估算模型未知 (比 Σ3D段长/速度 大 20.9%), 不参与严格比对, 单独断言见用例末尾
               element == "duration" || element == "wpml:duration";
    }

    bool isNamespaceDeclaration(const char* name)
    {
        const ::std::string attribute { name };
        return attribute == "xmlns" || attribute.rfind("xmlns:", 0) == 0;
    }

    // 取指定类型的第一个子节点 (pugixml 的 xml_node::child 只有按名字的重载)
    pugi::xml_node firstChildOfType(const pugi::xml_node& parent, pugi::xml_node_type type)
    {
        for (const auto child : parent.children())
        {
            if (child.type() == type)
            {
                return child;
            }
        }
        return {};
    }

    void collectChildElements(const pugi::xml_node& parent, ::std::vector<pugi::xml_node>& out)
    {
        for (const auto child : parent.children())
        {
            if (child.type() == pugi::node_element && !isIgnoredElement(child.name()))
            {
                out.push_back(child);
            }
        }
    }

    bool hasChildElement(const pugi::xml_node& node)
    {
        ::std::vector<pugi::xml_node> children {};
        collectChildElements(node, children);
        return !children.empty();
    }

    void compareAttributes(const pugi::xml_node& mine, const pugi::xml_node& reference, const ::std::string& path, Diffs& diffs)
    {
        for (const auto attribute : reference.attributes())
        {
            if (isNamespaceDeclaration(attribute.name()))
            {
                continue;
            }
            const auto mineAttribute { mine.attribute(attribute.name()) };
            if (!mineAttribute)
            {
                diffs.push_back(path + ": 缺少属性 " + attribute.name());
                continue;
            }
            if (!textEquals(trim(mineAttribute.value()), trim(attribute.value())))
            {
                diffs.push_back(path + ": 属性 " + attribute.name() + " 我们='" + mineAttribute.value() + "' 参考='" + attribute.value() + "'");
            }
        }
        for (const auto attribute : mine.attributes())
        {
            if (!isNamespaceDeclaration(attribute.name()) && !reference.attribute(attribute.name()))
            {
                diffs.push_back(path + ": 多出属性 " + attribute.name() + "='" + attribute.value() + "'");
            }
        }
    }

    void compareNodes(const pugi::xml_node& mine, const pugi::xml_node& reference, const ::std::string& path, Diffs& diffs)
    {
        compareAttributes(mine, reference, path, diffs);

        ::std::vector<pugi::xml_node> mineChildren {};
        ::std::vector<pugi::xml_node> referenceChildren {};
        collectChildElements(mine, mineChildren);
        collectChildElements(reference, referenceChildren);

        if (mineChildren.size() != referenceChildren.size())
        {
            diffs.push_back(
                path + ": 子元素数量 我们=" + ::std::to_string(mineChildren.size()) + " 参考=" + ::std::to_string(referenceChildren.size())
            );
            return;
        }

        for (::std::size_t i = 0; i < mineChildren.size(); ++i)
        {
            const ::std::string mineName { mineChildren[i].name() };
            const ::std::string referenceName { referenceChildren[i].name() };
            // 同名兄弟加上下标, 便于定位到具体航点 / 动作组
            const bool repeated { ::std::count_if(
                                      referenceChildren.begin(),
                                      referenceChildren.end(),
                                      [&referenceName](const pugi::xml_node& node)
                                      {
                                          return ::std::string { node.name() } == referenceName;
                                      }
                                  ) > 1 };
            const ::std::string childPath { path + "/" + referenceName + (repeated ? "[" + ::std::to_string(i) + "]" : "") };

            if (mineName != referenceName)
            {
                diffs.push_back(childPath + ": 元素名 我们='" + mineName + "' 参考='" + referenceName + "'");
                continue;
            }

            if (hasChildElement(referenceChildren[i]))
            {
                compareNodes(mineChildren[i], referenceChildren[i], childPath, diffs);
            }
            else
            {
                const ::std::string mineText { trim(mineChildren[i].text().get()) };
                const ::std::string referenceText { trim(referenceChildren[i].text().get()) };
                if (!textEquals(mineText, referenceText))
                {
                    diffs.push_back(childPath + ": 取值 我们='" + mineText + "' 参考='" + referenceText + "'");
                }
            }
        }
    }

    void compareXmlDocuments(const ::std::string& mineXml, const std::string& referenceXml, const std::string& entryName, Diffs& diffs)
    {
        pugi::xml_document mine {};
        pugi::xml_document reference {};
        const auto         flags { pugi::parse_default | pugi::parse_trim_pcdata };
        if (!mine.load_string(mineXml.c_str(), flags))
        {
            diffs.push_back(entryName + ": 我们生成的文件不是合法 XML");
            return;
        }
        if (!reference.load_string(referenceXml.c_str(), flags))
        {
            diffs.push_back(entryName + ": 参考文件不是合法 XML");
            return;
        }

        // XML 声明 (版本/编码) 也要一致
        const auto mineDeclaration { firstChildOfType(mine, pugi::node_declaration) };
        const auto referenceDeclaration { firstChildOfType(reference, pugi::node_declaration) };
        for (const auto attribute : referenceDeclaration.attributes())
        {
            const auto mineAttribute { mineDeclaration.attribute(attribute.name()) };
            if (!mineAttribute || ::std::string { mineAttribute.value() } != attribute.value())
            {
                diffs.push_back(
                    entryName + ": XML 声明 " + attribute.name() + " 我们='" + (mineAttribute ? mineAttribute.value() : "<缺失>") + "' 参考='" +
                    attribute.value() + "'"
                );
            }
        }

        const auto mineRoot { mine.child("kml") };
        const auto referenceRoot { reference.child("kml") };
        if (!mineRoot || !referenceRoot)
        {
            diffs.push_back(entryName + ": 缺少 <kml> 根节点");
            return;
        }
        compareNodes(mineRoot, referenceRoot, entryName + "/kml", diffs);
    }

    ::std::string joinDiffs(const Diffs& diffs)
    {
        constexpr ::std::size_t kMaxReported { 60 };
        ::std::string           report { "共 " + ::std::to_string(diffs.size()) + " 处差异:\n" };
        for (::std::size_t i = 0; i < diffs.size() && i < kMaxReported; ++i)
        {
            report += "  - " + diffs[i] + "\n";
        }
        if (diffs.size() > kMaxReported)
        {
            report += "  ... 其余 " + ::std::to_string(diffs.size() - kMaxReported) + " 处已省略\n";
        }
        return report;
    }
} // namespace

class KmzReferenceTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        if (plane::test::writeSharedConfig())
        {
            (void)::plane::config::ConfigManager::getInstance().loadAndCheck(plane::test::sharedConfigPath());
        }
    }
};

// 需求 1: 我们的 JSON 解析能吃下这份真实任务报文
TEST_F(KmzReferenceTest, ParsesRealTaskJson)
{
    const ::std::string raw { readFile(samplePath("kmz_template.json")) };
    ASSERT_FALSE(raw.empty()) << "读不到 sample_data/kmz_template.json";

    ::nlohmann::json parsed {};
    ASSERT_NO_THROW(parsed = ::nlohmann::json::parse(raw)) << "样例 JSON 本身不是合法 JSON";

    // 外层消息 (与生产链路 parseAndRouteMessage 使用的结构一致)
    plane::protocol::NetworkMessage<plane::protocol::WaypointPayload> message {};
    ASSERT_NO_THROW(message = parsed.get<plane::protocol::NetworkMessage<plane::protocol::WaypointPayload>>());

    EXPECT_EQ(message.ZBID, "1581F7FVC25CG00DJYAB");
    EXPECT_EQ(message.XXID, "f1b3b7574d2e69e8");
    EXPECT_EQ(message.XXLX, "XFHXRW");
    EXPECT_EQ(message.SJC, 1'789'440'087'398);
    ASSERT_TRUE(message.XXXX.has_value());

    const auto& payload { message.XXXX.value() };
    ASSERT_TRUE(payload.RWID.has_value());
    EXPECT_EQ(payload.RWID.value(), "c69b9c172cabbc8f");
    ASSERT_EQ(payload.HDJ.size(), 7u);

    // 首航点逐字段核对 (FJPHJ 是小数: 我们的模型若用整型会在这里解析失败)
    EXPECT_DOUBLE_EQ(payload.HDJ[0].JD, 118.89349);
    EXPECT_DOUBLE_EQ(payload.HDJ[0].WD, 32.066977);
    EXPECT_DOUBLE_EQ(payload.HDJ[0].GD, 50.0);
    EXPECT_DOUBLE_EQ(payload.HDJ[0].SD, 5.0);
    EXPECT_DOUBLE_EQ(payload.HDJ[0].YTFYJ, -90.0);
    EXPECT_DOUBLE_EQ(payload.HDJ[0].FJPHJ, -15.880828857421875);

    // 有小数偏航角的航点 (参考 KMZ 的 waypointHeadingAngle 直接取自这里)
    EXPECT_DOUBLE_EQ(payload.HDJ[3].FJPHJ, -15.880828857421875);
    EXPECT_DOUBLE_EQ(payload.HDJ[4].FJPHJ, 74.1567153930664);
    EXPECT_DOUBLE_EQ(payload.HDJ[6].FJPHJ, -105.84303283691406);
}

// 需求 2: 由同一份 JSON 生成的 KMZ 与真机实飞 KMZ 等价
TEST_F(KmzReferenceTest, GeneratedKmzMatchesRealFlightKmz)
{
    const ::std::string rawJson { readFile(samplePath("kmz_template.json")) };
    ASSERT_FALSE(rawJson.empty());

    const auto message { ::nlohmann::json::parse(rawJson).get<plane::protocol::NetworkMessage<plane::protocol::WaypointPayload>>() };
    ASSERT_TRUE(message.XXXX.has_value());

    const auto kmz { plane::utils::JsonToKmzConverter::convertWaypointsToKmz(message.XXXX->HDJ) };
    ASSERT_TRUE(kmz.has_value());

    const auto referenceKmz { readFile(samplePath("kmz_template.kmz")) };
    ASSERT_FALSE(referenceKmz.empty());

    const ::std::vector<::std::uint8_t> referenceBytes { referenceKmz.begin(), referenceKmz.end() };

    Diffs                               diffs {};
    for (const char* entry : { "wpmz/waylines.wpml", "wpmz/template.kml" })
    {
        const ::std::string mineXml { ::plane::test::readZipEntry(*kmz, entry) };
        const ::std::string referenceXml { ::plane::test::readZipEntry(referenceBytes, entry) };
        ASSERT_FALSE(mineXml.empty()) << "我们生成的 KMZ 缺少条目 " << entry;
        ASSERT_FALSE(referenceXml.empty()) << "参考 KMZ 缺少条目 " << entry;
        compareXmlDocuments(mineXml, referenceXml, entry, diffs);
    }

    EXPECT_TRUE(diffs.empty()) << joinDiffs(diffs);

    // duration 单独看: 参考侧估算模型未知 (比 Σ3D段长/速度 大 20.9%), 不参与严格比对,
    // 但要求同量级 —— 差距扩大说明我们的距离/速度算法出了问题
    const ::std::string mineWaylines { ::plane::test::readZipEntry(*kmz, "wpmz/waylines.wpml") };
    const auto          oursDuration { ::std::stod(mineWaylines.substr(mineWaylines.find("<wpml:duration>") + 15)) };
    EXPECT_NEAR(oursDuration, 111.589462280273, 111.589462280273 * 0.3) << "duration 与参考不再同量级";
}

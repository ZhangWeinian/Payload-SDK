// cy_psdk/tests/test_exehomepath.cpp
//
// EXEHomePath: 以 argv[0] 定位交付目录的纯逻辑测试。

#include "utils/EXEHomePath.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

namespace
{
	using plane::utils::getEXEHomePath;
} // namespace

TEST(ExeHomePath, ResolvesDirectoryFromAbsoluteArgv0)
{
	getEXEHomePath.init("/opt/cy_psdk/bin/cy_psdk");

	EXPECT_EQ(getEXEHomePath("").parent_path().string(), "/opt/cy_psdk/bin");
	EXPECT_EQ(getEXEHomePath("config.yml").string(), "/opt/cy_psdk/bin/config.yml");
	EXPECT_EQ(getEXEHomePath("libs").string(), "/opt/cy_psdk/bin/libs");
}

TEST(ExeHomePath, ResolvesRelativeArgv0AgainstCurrentDirectory)
{
	getEXEHomePath.init("cy_psdk");

	// 相对路径 -> current_path/cy_psdk 的父目录
	const auto expected { std::filesystem::weakly_canonical(std::filesystem::current_path() / "cy_psdk").parent_path() };
	EXPECT_EQ(getEXEHomePath("").parent_path().string(), expected.string());
}

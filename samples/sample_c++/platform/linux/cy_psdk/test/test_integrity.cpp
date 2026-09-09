// cy_psdk/tests/test_integrity.cpp
//
// 部署完整性自检黑盒测试: 在临时目录构造 {cy_psdk, libs/, *.sha256} 交付结构,
// 经 verifyDeploymentIntegrity(argv0) 验证通过与篡改拒绝。不依赖 loader 真实加载。

#include "utils/integrity/IntegrityCheck.h"

#include "define.h"

#include <gtest/gtest.h>

#include <openssl/evp.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace
{
	namespace fs = _STD filesystem;

	_STD string			sha256HexOfFile(const fs::path& file)
	{
		_STD ifstream in(file, _STD ios::binary);
		_STD unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx { EVP_MD_CTX_new(), EVP_MD_CTX_free };
		if (!in || !ctx || EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) != 1)
		{
			return {};
		}

		char buf[4096];
		while (in.good())
		{
			in.read(buf, sizeof(buf));
			if (auto n { in.gcount() }; n > 0 && EVP_DigestUpdate(ctx.get(), buf, static_cast<_STD size_t>(n)) != 1)
			{
				return {};
			}
		}

		unsigned char digest[EVP_MAX_MD_SIZE] {};
		unsigned int  digest_len { 0 };
		if (EVP_DigestFinal_ex(ctx.get(), digest, &digest_len) != 1)
		{
			return {};
		}

		constexpr static char hex[] { "0123456789abcdef" };
		_STD string			  out {};
		out.reserve(digest_len * 2);
		for (unsigned int i { 0 }; i < digest_len; ++i)
		{
			out.push_back(hex[digest[i] >> 4]);
			out.push_back(hex[digest[i] & 0X0f]);
		}
		return out;
	}

	void writeFile(const fs::path& file, const _STD string& content)
	{
		_STD ofstream out(file, _STD ios::binary);
		out << content;
	}

	void writeChecksumFor(const fs::path& file)
	{
		writeFile(fs::path(file.string() + ".sha256"), sha256HexOfFile(file) + "\n");
	}

	// 构造标准交付目录, 返回 base 路径 (argv0 = base/cy_psdk)
	fs::path makeDeployment(const _STD string& tag)
	{
		auto base { fs::temp_directory_path() / ("cy_psdk_integrity_" + tag) };
		fs::remove_all(base);
		fs::create_directories(base / "libs");

		writeFile(base / "cy_psdk", "fake main binary\n");
		writeFile(base / "libs" / "liba.so", "fake lib a\n");
		writeFile(base / "libs" / "libb.so", "fake lib b\n");
		writeChecksumFor(base / "cy_psdk");
		writeChecksumFor(base / "libs" / "liba.so");
		writeChecksumFor(base / "libs" / "libb.so");
		return base;
	}
} // namespace

TEST(IntegrityCheck, PassesForIntactDeployment)
{
	const auto base { makeDeployment("ok") };
	EXPECT_TRUE(plane::utils::verifyDeploymentIntegrity((base / "cy_psdk").c_str()));
}

TEST(IntegrityCheck, RejectsTamperedLibrary)
{
	const auto base { makeDeployment("tampered") };

	// 篡改库文件但不更新校验文件 -> 哈希不匹配
	writeFile(base / "libs" / "liba.so", "tampered content\n");

	EXPECT_FALSE(plane::utils::verifyDeploymentIntegrity((base / "cy_psdk").c_str()));
}

TEST(IntegrityCheck, RejectsTamperedMainBinary)
{
	const auto base { makeDeployment("tampered_main") };

	writeFile(base / "cy_psdk", "tampered main\n");

	EXPECT_FALSE(plane::utils::verifyDeploymentIntegrity((base / "cy_psdk").c_str()));
}

TEST(IntegrityCheck, RejectsLibraryWithoutChecksum)
{
	const auto base { makeDeployment("missing_sha") };

	// 新增一个没有 .sha256 的库文件 -> 必检失败
	writeFile(base / "libs" / "libc.so", "no checksum\n");

	EXPECT_FALSE(plane::utils::verifyDeploymentIntegrity((base / "cy_psdk").c_str()));
}

TEST(IntegrityCheck, RejectsMissingLibsDirectory)
{
	auto base { fs::temp_directory_path() / "cy_psdk_integrity_nolibs" };
	fs::remove_all(base);
	fs::create_directories(base);

	writeFile(base / "cy_psdk", "fake main\n");
	writeChecksumFor(base / "cy_psdk");

	EXPECT_FALSE(plane::utils::verifyDeploymentIntegrity((base / "cy_psdk").c_str()));
}

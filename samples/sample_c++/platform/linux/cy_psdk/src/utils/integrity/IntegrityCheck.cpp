// cy_psdk/utils/integrity/IntegrityCheck.cpp

#include "utils/integrity/IntegrityCheck.h"

#include "utils/log_util/Logger.h"

#include "define.h"

#include <openssl/evp.h>

#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

namespace
{
	// 由 argv[0] 推导交付目录 (cy_psdk 所在目录)
	_STD_FS path getExeDirectory(const char* argv0)
	{
		_STD error_code ec;
		if (argv0 != nullptr && argv0[0] != '\0')
		{
			_STD_FS path exe { argv0 };
			if (exe.is_relative())
			{
				exe = _STD_FS current_path(ec) / exe;
			}
			if (auto canonical { _STD_FS weakly_canonical(exe, ec) }; !ec)
			{
				return canonical.parent_path();
			}
		}

		// 兜底: /proc/self/exe (常规直接启动时有效; loader 显式启动时指向解释器, 仅作兜底)
		auto self { _STD_FS read_symlink("/proc/self/exe", ec) };
		if (!ec && !self.empty())
		{
			return self.parent_path();
		}
		return _STD_FS current_path(ec);
	}

	// 计算文件 SHA256, 输出小写 hex; 失败返回 false
	bool computeSha256Hex(const _STD_FS path& file, _STD string& hex_out)
	{
		_STD ifstream in(file, _STD ios::binary);
		if (!in)
		{
			return false;
		}

		_STD unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx { EVP_MD_CTX_new(), EVP_MD_CTX_free };
		if (!ctx || EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) != 1)
		{
			return false;
		}

		_STD array<char, 64 * 1024> buf {};
		while (in.good())
		{
			in.read(buf.data(), static_cast<_STD streamsize>(buf.size()));
			auto n { in.gcount() };
			if (n > 0 && EVP_DigestUpdate(ctx.get(), buf.data(), static_cast<_STD size_t>(n)) != 1)
			{
				return false;
			}
		}

		unsigned char digest[EVP_MAX_MD_SIZE] {};
		unsigned int  digest_len { 0 };
		if (EVP_DigestFinal_ex(ctx.get(), digest, &digest_len) != 1)
		{
			return false;
		}

		_STD ostringstream oss;
		oss << _STD hex << _STD setfill('0');
		for (unsigned int i { 0 }; i < digest_len; ++i)
		{
			oss << _STD setw(2) << static_cast<unsigned int>(digest[i]);
		}
		hex_out = oss.str();
		return true;
	}

	// 读取 .sha256 文件中第一段文本作为期望哈希
	bool readStoredHash(const _STD_FS path& checksum_file, _STD string& hash_out)
	{
		_STD ifstream in(checksum_file);
		if (!in)
		{
			return false;
		}
		in >> hash_out;
		return !hash_out.empty();
	}

	// 校验单个文件; required=false 时缺失校验文件仅告警不判失败
	bool verifyFile(const _STD_FS path& file, bool required)
	{
		_STD string expected;
		_STD string actual;
		const auto	checksum_file { _STD string(file.string()) + ".sha256" };

		if (!readStoredHash(checksum_file, expected))
		{
			if (required)
			{
				LOG_ERROR("完整性自检失败: {} 缺少校验文件", file.filename().string());
				return false;
			}
			LOG_WARN("完整性自检: {} 缺少校验文件, 跳过", file.filename().string());
			return true;
		}

		if (!computeSha256Hex(file, actual))
		{
			LOG_ERROR("完整性自检失败: 无法读取 {}", file.filename().string());
			return false;
		}

		if (actual != expected)
		{
			LOG_ERROR("完整性自检失败: {} 哈希不匹配\n  期望: {}\n  实际: {}", file.filename().string(), expected, actual);
			return false;
		}

		LOG_INFO("完整性自检通过: {}", file.filename().string());
		return true;
	}
} // namespace

namespace plane::utils
{
	bool verifyDeploymentIntegrity(const char* argv0)
	{
		// 开发调试用跳过开关
		if (const char* skip { _CSTD getenv("CY_PSDK_SKIP_INTEGRITY") }; skip != nullptr && _CSTD strcmp(skip, "1") == 0)
		{
			LOG_WARN("检测到 CY_PSDK_SKIP_INTEGRITY=1, 跳过部署完整性自检");
			return true;
		}

		LOG_INFO("开始部署完整性自检 (SHA256) ...");

		bool all_ok { true };

		// 1) 主程序本身 (必须有校验文件)
		const auto exe_dir { getExeDirectory(argv0) };
		const auto exe_path { exe_dir / "cy_psdk" };
		if (!_STD_FS exists(exe_path) || !verifyFile(exe_path, true))
		{
			all_ok = false;
		}

		// 2) libs/ 下所有运行库 (构建时会为每个库生成 .sha256)
		const auto libs_dir { exe_dir / "libs" };
		if (_STD_FS is_directory(libs_dir))
		{
			for (const auto& entry : _STD_FS directory_iterator(libs_dir))
			{
				if (!entry.is_regular_file())
				{
					continue;
				}
				const auto& file { entry.path() };
				if (file.extension() == ".sha256")
				{
					continue;
				}
				if (!verifyFile(file, true))
				{
					all_ok = false;
				}
			}
		}
		else
		{
			LOG_ERROR("完整性自检失败: 未找到运行库目录 libs/ ({})", libs_dir.string());
			all_ok = false;
		}

		if (all_ok)
		{
			LOG_INFO("部署完整性自检全部通过");
		}
		return all_ok;
	}
} // namespace plane::utils

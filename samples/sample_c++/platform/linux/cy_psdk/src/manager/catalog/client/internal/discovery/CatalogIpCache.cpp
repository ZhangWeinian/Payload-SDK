// cy_psdk/manager/catalog/client/internal/discovery/CatalogIpCache.cpp

#include "manager/catalog/client/internal/discovery/CatalogIpCache.h"

#include <system_error>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <unistd.h>

#include "manager/catalog/client/internal/util/TargetExpander.h"
#include "manager/catalog/client/internal/util/TextUtil.h"

#include "define.h"

namespace plane::catalog::internal
{
	CatalogIpCache::CatalogIpCache(_STD string file): file_(_STD move(file)) {}

	_STD vector<_STD string> CatalogIpCache::prioritize(const _STD vector<_STD string>& targets)
	{
		_STD vector<_STD string> result { targets };
		_STD ifstream			 input { this->file_, _STD ios::binary };
		if (!input.is_open())
		{
			return result;
		}
		_STD string		  line {};
		_STD			  getline(input, line);
		const _STD string cached { trimAsciiWhitespaceCopy(line) };
		if (cached.empty())
		{
			return result;
		}
		for (_STD size_t index { 0 }; index < result.size(); ++index)
		{
			if (result[index] == cached && index > 0)
			{
				result.erase(result.begin() + static_cast<_STD ptrdiff_t>(index));
				result.insert(result.begin(), cached);
				break;
			}
		}
		return result;
	}

	void CatalogIpCache::save(const _STD string& ip)
	{
		// 只保存规范单 IP
		const _STD vector<_STD string>	 single { ip };
		Result<_STD vector<_STD string>> checked { expandTargets(single, 1) };
		if (!checked.isOk())
		{
			return;
		}

		const _STD filesystem::path path { this->file_ };
		_STD error_code				ec {};
		const auto					parent { path.parent_path() };
		if (!parent.empty())
		{
			_STD filesystem::create_directories(parent, ec);
			if (ec)
			{
				return;
			}
		}

		// 临时文件建在目标目录内 (对齐 java Files.createTempFile(parent, ...)):
		// 与目标同文件系统, 保证 rename 原子生效; fd 直写, 避免二次 close 误关他人 fd
		const _STD filesystem::path temp_template { path.parent_path() / (path.filename().string() + ".tmp.XXXXXX") };
		const _STD string			temp_name { temp_template.string() };
		_STD vector<char> writable { temp_name.begin(), temp_name.end() };
		writable.push_back('\0');
		const int fd { _CSTD mkstemp(writable.data()) };
		if (fd < 0)
		{
			return;
		}
		const _STD string	temp_path { writable.data() };
		const _STD string	payload { ip + "\n" };
		const _CSTD ssize_t written { _CSTD write(fd, payload.data(), payload.size()) };
		const bool			write_ok { written == static_cast<_CSTD ssize_t>(payload.size()) };
		if (_CSTD close(fd) != 0 || !write_ok)
		{
			_CSTD remove(temp_path.c_str());
			return;
		}

		_STD filesystem::rename(temp_path, path, ec);
		if (ec)
		{
			// 跨文件系统等场景: 回退复制后删除临时文件 (对齐 java REPLACE_EXISTING 回退)
			_STD error_code fallback_ec {};
			_STD			filesystem::copy_file(temp_path, path, _STD filesystem::copy_options::overwrite_existing, fallback_ec);
			_STD			filesystem::remove(temp_path, fallback_ec);
		}
	}
} // namespace plane::catalog::internal

// cy_psdk/manager/catalog/client/internal/CatalogIpCache.cpp

#include "manager/catalog/client/internal/CatalogIpCache.h"

#include <system_error>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <unistd.h>

#include "manager/catalog/client/internal/TargetExpander.h"

#include "define.h"

namespace plane::catalog::internal
{
	namespace
	{
		_NODISCARD _STD string trimText(const _STD string& value)
		{
			const _STD size_t begin { value.find_first_not_of(" \t\r\n") };
			if (begin == _STD string::npos)
			{
				return "";
			}
			const _STD size_t end { value.find_last_not_of(" \t\r\n") };
			return value.substr(begin, end - begin + 1);
		}
	} // namespace

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
		const _STD string cached { trimText(line) };
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

		// 临时文件 + 原子 rename
		const _STD string temp_name { path.filename().string() + ".tmp.XXXXXX" };
		_STD vector<char> writable { temp_name.begin(), temp_name.end() };
		writable.push_back('\0');
		const int fd { ::mkstemp(writable.data()) };
		if (fd < 0)
		{
			return;
		}
		::close(fd);
		const _STD string temp_path { writable.data() };
		{
			_STD ofstream out { temp_path, _STD ios::binary | _STD ios::trunc };
			if (!out.is_open())
			{
				::close(fd);
				::remove(temp_path.c_str());
				return;
			}
			out << ip << '\n';
			out.flush();
			if (!out.good())
			{
				out.close();
				::remove(temp_path.c_str());
				return;
			}
			out.close();
		}

		_STD filesystem::rename(temp_path, path, ec);
		if (ec)
		{
			::remove(temp_path.c_str());
		}
	}
} // namespace plane::catalog::internal

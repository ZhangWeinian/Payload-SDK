// cy_psdk/utils/EXEHomePath.h

#pragma once

#include <filesystem>

#include "define.h"

namespace plane::utils
{
	class __Get_EXE_home_path_fun: private __Not_quite_object
	{
	public:
		using __Not_quite_object::__Not_quite_object;

		// 获取可执行文件所在目录路径
		_NODISCARD _STD_FS path operator()(const _STD_FS path& childPath = "") noexcept
		{
			if (this->exe_home_path_.empty())
			{
				try
				{
					this->exe_home_path_ = _STD_FS read_symlink("/proc/self/exe").parent_path();
				}
				catch (const _STD_FS filesystem_error& e)
				{}
			}

			return this->exe_home_path_ / childPath;
		}

	private:
		_STD_FS path exe_home_path_ {};
	};

	inline __Get_EXE_home_path_fun getEXEHomePath { __Not_quite_object::__Construct_tag {} };
} // namespace plane::utils

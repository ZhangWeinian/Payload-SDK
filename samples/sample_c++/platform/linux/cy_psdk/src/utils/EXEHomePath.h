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

		// 在启动最早处用 argv[0] 显式初始化"交付目录"。
		// 注意: 经打包解释器 (ld-linux-*) 显式启动时, /proc/self/exe 指向解释器所在的 libs/,
		//       直接依赖它会导致 config.yml/日志路径全部错位, 因此必须以 argv[0] 为准。
		void init(const char* argv0) noexcept
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
					this->exe_home_path_ = canonical.parent_path();
				}
			}
		}

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

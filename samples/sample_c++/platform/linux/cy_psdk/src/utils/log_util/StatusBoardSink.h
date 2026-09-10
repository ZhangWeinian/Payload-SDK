// cy_psdk/utils/log_util/StatusBoardSink.h
//
// 终端状态板的 spdlog 控制台接收器: 日志输出前自动"让位"给状态块
// (抹除状态块 -> 打印日志 -> 重绘状态块), 保证连接状态等关键信息不被日志淹没。
// 非交互终端下行为与普通 stdout 接收器等价 (StatusBoard 自动降级)。
//
// 颜色: 仅在终端支持 ANSI 颜色时 (同 StatusBoard: isatty + TERM 非 dumb +
// 未设置 NO_COLOR) 按 color_range (%^...%$) 与日志级别着色, 方案对齐 spdlog
// ansicolor 接收器; 重定向到文件等场景输出纯文本 (与 spdlog automatic 模式一致)。

#pragma once

#include <mutex>

#include <spdlog/common.h>
#include <spdlog/sinks/base_sink.h>

#include "define.h"
#include "utils/status_board/StatusBoard.h"

namespace plane::utils
{
	class StatusBoardSink final: public _SPDLOG sinks::base_sink<_STD mutex>
	{
	public:
		StatusBoardSink(void) noexcept					   = default;
		~StatusBoardSink(void) noexcept override		   = default;
		StatusBoardSink(const StatusBoardSink&)			   = delete;
		StatusBoardSink& operator=(const StatusBoardSink&) = delete;

	protected:
		void sink_it_(const _SPDLOG details::log_msg& msg) override
		{
			_SPDLOG memory_buf_t formatted;
			this->formatter_->format(msg, formatted);

			const char* const data { formatted.data() };
			const _STD size_t size { formatted.size() };
			const auto		  rangeStart { msg.color_range_start };
			const auto		  rangeStop { msg.color_range_end };

			_STD string		  text;
			if (StatusBoard::colorSupported() && rangeStop > rangeStart && rangeStop <= size)
			{
				// 仅对 %^...%$ 标记的区间着色 (与 spdlog ansicolor 接收器行为一致)
				text.reserve(size + 16);
				text.append(data, rangeStart);
				text.append(levelColorCode(msg.level));
				text.append(data + rangeStart, rangeStop - rangeStart);
				text.append("\033[0m");
				text.append(data + rangeStop, size - rangeStop);
			}
			else
			{
				text.assign(data, size);
			}

			StatusBoard::getInstance().log(text);
		}

		void flush_(void) override {}

	private:
		// 日志级别颜色映射 (对齐 spdlog ansicolor 接收器的默认配色)
		_NODISCARD static const char* levelColorCode(_SPDLOG level::level_enum level) noexcept
		{
			switch (level)
			{
				case _SPDLOG level::trace:
					return "\033[37m";	 // 白
				case _SPDLOG level::debug:
					return "\033[36m";	 // 青
				case _SPDLOG level::info:
					return "\033[32m";	 // 绿
				case _SPDLOG level::warn:
					return "\033[33m";	 // 黄
				case _SPDLOG level::err:
					return "\033[31m";	 // 红
				case _SPDLOG level::critical:
					return "\033[1;31m"; // 粗红
				case _SPDLOG level::off:
				default:
					return "";
			}
		}
	};
} // namespace plane::utils

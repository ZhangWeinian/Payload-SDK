// cy_psdk/utils/status_board/StatusBoard.cpp

#include "utils/status_board/StatusBoard.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <sys/ioctl.h>
#include <unistd.h>

namespace
{
	// ---------------------------------------------------------------- 终端环境探测
	_NODISCARD bool stdoutIsTerminal() noexcept
	{
		return _CSTD isatty(STDOUT_FILENO) != 0;
	}

	_NODISCARD bool terminalSupportsAnsi() noexcept
	{
		const char*						term { _CSTD getenv("TERM") };
		return term != nullptr && _CSTD strcmp(term, "dumb") != 0;
	}

	_NODISCARD bool colorEnabled() noexcept
	{
		return _CSTD getenv("NO_COLOR") == nullptr && terminalSupportsAnsi();
	}

	_NODISCARD int terminalWidth() noexcept
	{
		struct winsize ws {};
		if (_CSTD ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
		{
			return static_cast<int>(ws.ws_col);
		}
		return 0; // 未知宽度, 不做截断
	}

	// UTF-8 近似显示宽度: ASCII 1 列; 双字节码点 1 列; 三/四字节码点 (CJK/emoji) 2 列。
	// 注意: 保守估算, 宁可略宽也不让状态行折行 (折行会破坏状态块的固定行数)。
	_NODISCARD int displayWidth(_STD string_view text) noexcept
	{
		int width { 0 };
		for (size_t i = 0; i < text.size();)
		{
			const unsigned char b { static_cast<unsigned char>(text[i]) };
			size_t				len { 1 };
			if ((b & 0Xe0) == 0Xc0)
			{
				len = 2;
			}
			else if ((b & 0Xf0) == 0Xe0)
			{
				len = 3;
			}
			else if ((b & 0Xf8) == 0Xf0)
			{
				len = 4;
			}
			width += (b < 0X80) ? 1 : ((len == 2) ? 1 : 2);
			i	  += _STD min(len, text.size() - i);
		}
		return width;
	}

	_NODISCARD _STD string truncateToWidth(_STD string_view text, int maxWidth)
	{
		if (maxWidth <= 0 || displayWidth(text) <= maxWidth)
		{
			return _STD string { text };
		}
		_STD string out;
		int			width { 0 };
		size_t		i { 0 };
		while (i < text.size())
		{
			const unsigned char b { static_cast<unsigned char>(text[i]) };
			size_t				len { 1 };
			if ((b & 0Xe0) == 0Xc0)
			{
				len = 2;
			}
			else if ((b & 0Xf0) == 0Xe0)
			{
				len = 3;
			}
			else if ((b & 0Xf8) == 0Xf0)
			{
				len = 4;
			}
			len = _STD min(len, text.size() - i);
			const int  w { (b < 0X80) ? 1 : ((len == 2) ? 1 : 2) };
			if (width + w > maxWidth - 1) // 预留 1 列给省略号
			{
				out += "…";
				break;
			}
			out.append(text.substr(i, len));
			width += w;
			i	  += len;
		}
		return out;
	}
} // namespace

namespace plane::utils
{
	StatusBoard& StatusBoard::getInstance(void) noexcept
	{
		static StatusBoard instance {};
		return instance;
	}

	void StatusBoard::setEnabled(bool enabled) noexcept
	{
		enabled_ = enabled;
	}

	bool StatusBoard::isEnabled(void) const noexcept
	{
		return enabled_;
	}

	bool StatusBoard::isInteractive(void) const noexcept
	{
		return enabled_ && stdoutIsTerminal() && terminalSupportsAnsi();
	}

	bool StatusBoard::colorSupported(void) noexcept
	{
		// 仅在输出目标确为终端时着色 (与 spdlog ansicolor automatic 模式一致);
		// 重定向到文件/管道时输出纯文本, 避免日志文件被转义序列污染
		return stdoutIsTerminal() && colorEnabled();
	}

	void StatusBoard::update(const _STD string& key, _STD string value, StatusLevel level)
	{
		_STD lock_guard lock { mutex_ };
		Item&			item { this->findOrCreateLocked(key) };
		if (item.value == value && item.level == level)
		{
			return; // 值未变化, 不产生任何输出
		}
		item.value = _STD move(value);
		item.level = level;
		if (this->interactiveLocked())
		{
			this->eraseBlockLocked();
			this->drawBlockLocked();
			this->flushLocked();
		}
		else if (enabled_)
		{
			this->writePlainLocked("[状态] " + key + " = " + item.value);
		}
	}

	void StatusBoard::log(_STD string_view line)
	{
		_STD lock_guard lock { mutex_ };
		if (!this->interactiveLocked())
		{
			this->writePlainLocked(_STD string { line });
			return;
		}
		this->eraseBlockLocked();
		this->writeLogLocked(line);
		this->drawBlockLocked();
		this->flushLocked();
	}

	void StatusBoard::finish(void) noexcept
	{
		_STD lock_guard lock { mutex_ };
		if (this->interactiveLocked())
		{
			this->eraseBlockLocked();
		}
		this->flushLocked();
	}

	bool StatusBoard::interactiveLocked(void) const noexcept
	{
		return enabled_ && stdoutIsTerminal() && terminalSupportsAnsi();
	}

	StatusBoard::Item& StatusBoard::findOrCreateLocked(const _STD string& key)
	{
		auto it { _STD find_if(
			items_.begin(),
			items_.end(),
			[&key](const Item& item)
			{
				return item.key == key;
			}
		) };
		if (it != items_.end())
		{
			return *it;
		}
		items_.push_back(Item { .key = key, .value = {}, .level = StatusLevel::Info });
		return items_.back();
	}

	void StatusBoard::flushLocked(void)
	{
		if (buffer_.empty())
		{
			return;
		}
		// write 可能被信号中断 (EINTR) 或短写: 循环写尽, 避免丢失输出
		size_t offset { 0 };
		while (offset < buffer_.size())
		{
			const _CSTD ssize_t written { _CSTD write(STDOUT_FILENO, buffer_.data() + offset, buffer_.size() - offset) };
			if (written < 0)
			{
				if (errno == EINTR)
				{
					continue;
				}
				break; // 其它错误 (如 EPIPE): 放弃本批输出, 不重试
			}
			offset += static_cast<size_t>(written);
		}
		buffer_.clear();
	}

	void StatusBoard::writePlainLocked(const _STD string& line)
	{
		// 剥离调用方可能自带的行尾, 统一追加单个换行 (避免重定向到文件时出现空行)
		_STD string_view view { line };
		while (!view.empty() && (view.back() == '\n' || view.back() == '\r'))
		{
			view.remove_suffix(1);
		}
		buffer_.append(view);
		buffer_.push_back('\n');
		this->flushLocked();
	}

	void StatusBoard::writeLogLocked(_STD string_view text)
	{
		for (const char c : text)
		{
			if (c == '\n')
			{
				buffer_.append("\r\n");
			}
			else
			{
				buffer_.push_back(c);
			}
		}
		if (text.empty() || text.back() != '\n')
		{
			buffer_.append("\r\n");
		}
	}

	// 状态块当前位于"已输出内容末尾", 光标停在块最后一行末尾 (无尾换行)。
	// 因此上移 (行数-1) 行即可回到块首, 再用 \033[J 清除到屏尾, 即完整抹除。
	void StatusBoard::eraseBlockLocked(void)
	{
		if (drawn_lines_ <= 0)
		{
			return;
		}
		if (drawn_lines_ > 1)
		{
			buffer_.append("\033[");
			buffer_.append(_STD to_string(drawn_lines_ - 1));
			buffer_.append("A");
		}
		buffer_.append("\r\033[J");
		drawn_lines_ = 0;
	}

	// 状态块布局: 每行 2~3 项; 列起点固定为终端宽度的等分位置 (行首 / 1/2; 或 1/3, 2/3),
	// 不随内容长度漂移; 放不下时降级 3 列 -> 2 列 -> 1 列。
	void StatusBoard::drawBlockLocked(void)
	{
		if (items_.empty())
		{
			return;
		}
		// 宽度探测失败 (0x0 pty / 无窗口尺寸的串口终端) 时按 80 列保守布局, 防止折行破坏块结构
		constexpr int FALLBACK_WIDTH { 80 };
		const int	  measuredWidth { terminalWidth() };
		const int	  width { measuredWidth > 0 ? measuredWidth : FALLBACK_WIDTH };
		const bool	  colors { StatusBoard::colorSupported() };

		// 各项的纯文本与最大宽度 (用于决定列数)
		_STD vector<_STD string> plains;
		plains.reserve(items_.size());
		int maxItemWidth { 0 };
		for (const Item& item : items_)
		{
			plains.push_back(item.key + " : " + item.value);
			maxItemWidth = _STD max(maxItemWidth, displayWidth(plains.back()));
		}

		constexpr int GAP_WIDTH { 3 }; // 相邻列之间的最小间隔 (空格数)

		// 列数: 目标 2~3 列; 放不下时逐级降级 (3 -> 2 -> 1)
		int columns { 1 };
		for (int candidate { 3 }; candidate >= 2; --candidate)
		{
			// 每个等分格需容纳最宽项, 并预留 1 列间隔
			const int cellWidth { width / candidate - GAP_WIDTH - 1 };
			if (cellWidth >= maxItemWidth)
			{
				columns = candidate;
				break;
			}
		}

		buffer_.append("\r");
		int lines { 0 };
		for (size_t i = 0; i < items_.size(); i += static_cast<size_t>(columns))
		{
			const size_t rowEnd { _STD min(items_.size(), i + static_cast<size_t>(columns)) };
			int			 cursorPos { 0 }; // 本行已输出到的显示列位置
			for (size_t j = i; j < rowEnd; ++j)
			{
				const size_t col { j - i };
				const bool	 lastInRow { j + 1 == rowEnd };

				// 本格起点: 终端宽度的固定等分位置
				const int startPos { static_cast<int>(static_cast<long long>(width) * static_cast<int>(col) / columns) };
				const int endPos { lastInRow ? width : static_cast<int>(static_cast<long long>(width) * (static_cast<int>(col) + 1) / columns) };
				const int avail { _STD max(endPos - startPos - 1, 1) }; // 预留 1 列间隔

				const int pad { startPos - cursorPos };
				if (pad > 0)
				{
					buffer_.append(static_cast<size_t>(pad), ' ');
				}

				const Item&		   item { items_[j] };
				const _STD string& plain { plains[j] };
				_STD string		   text { plain };
				if (displayWidth(text) > avail)
				{
					text = truncateToWidth(text, avail); // 超宽截断 (放弃着色)
				}

				if (colors && text == plain)
				{
					buffer_.append("\033[36m");
					buffer_.append(item.key);
					buffer_.append("\033[0m : ");
					buffer_.append(levelColor(item.level));
					buffer_.append(item.value);
					buffer_.append("\033[0m");
				}
				else
				{
					buffer_.append(text);
				}
				cursorPos = startPos + displayWidth(text);
			}
			++lines;
			if (rowEnd < items_.size())
			{
				buffer_.append("\r\n");
			}
		}
		// 末行不加换行: 光标停在状态块最后一行, 供下次 eraseBlockLocked 定位
		drawn_lines_ = lines;
	}

	const char* StatusBoard::levelColor(StatusLevel level) noexcept
	{
		switch (level)
		{
			case StatusLevel::Ok:
				return "\033[32m";
			case StatusLevel::Warn:
				return "\033[33m";
			case StatusLevel::Error:
				return "\033[31m";
			case StatusLevel::Info:
			default:
				return "";
		}
	}
} // namespace plane::utils

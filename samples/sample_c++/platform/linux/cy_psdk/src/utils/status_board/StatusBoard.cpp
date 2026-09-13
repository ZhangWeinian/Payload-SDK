// cy_psdk/utils/status_board/StatusBoard.cpp

#include "utils/status_board/StatusBoard.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>

#include <sys/ioctl.h>
#include <unistd.h>

namespace
{
    // 终端环境探测
    [[nodiscard]] bool stdoutIsTerminal() noexcept
    {
        return ::isatty(STDOUT_FILENO) != 0;
    }

    [[nodiscard]] bool terminalSupportsAnsi() noexcept
    {
        const char* term { ::getenv("TERM") };
        return term != nullptr && ::strcmp(term, "dumb") != 0;
    }

    [[nodiscard]] bool colorEnabled() noexcept
    {
        return ::getenv("NO_COLOR") == nullptr && terminalSupportsAnsi();
    }

    [[nodiscard]] int terminalWidth() noexcept
    {
        struct winsize ws {};
        if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        {
            return static_cast<int>(ws.ws_col);
        }
        return 0; // 未知宽度, 不做截断
    }

    // UTF-8 近似显示宽度: ASCII 1 列; 双字节码点 1 列; 三/四字节码点 (CJK/emoji) 2 列。
    // 注意: 保守估算, 宁可略宽也不让状态行折行 (折行会破坏状态块的固定行数)。
    [[nodiscard]] int displayWidth(::std::string_view text) noexcept
    {
        int width { 0 };
        for (size_t i = 0; i < text.size();)
        {
            const unsigned char b { static_cast<unsigned char>(text[i]) };
            size_t              len { 1 };
            if ((b & 0Xe0u) == 0Xc0)
            {
                len = 2;
            }
            else if ((b & 0Xf0u) == 0Xe0)
            {
                len = 3;
            }
            else if ((b & 0Xf8u) == 0Xf0)
            {
                len = 4;
            }
            width += (b < 0X80) ? 1 : ((len == 2) ? 1 : 2);
            i     += ::std::min(len, text.size() - i);
        }
        return width;
    }

    // 截断到 maxWidth 显示列并追加省略号。前置条件: 调用方保证 maxWidth >= 1
    // 且 displayWidth(text) > maxWidth (仅超宽时调用), 因此无需"未超宽直接返回"的保护分支
    [[nodiscard]] ::std::string truncateToWidth(::std::string_view text, int maxWidth)
    {
        ::std::string out;
        int           width { 0 };
        size_t        i { 0 };
        while (i < text.size())
        {
            const unsigned char b { static_cast<unsigned char>(text[i]) };
            size_t              len { 1 };
            if ((b & 0Xe0u) == 0Xc0)
            {
                len = 2;
            }
            else if ((b & 0Xf0u) == 0Xe0)
            {
                len = 3;
            }
            else if ((b & 0Xf8u) == 0Xf0)
            {
                len = 4;
            }
            len = ::std::min(len, text.size() - i);
            const int w { (b < 0X80) ? 1 : ((len == 2) ? 1 : 2) };
            if (width + w > maxWidth - 1) // 预留 1 列给省略号
            {
                out += "…";
                break;
            }
            out.append(text.substr(i, len));
            width += w;
            i     += len;
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
        this->enabled_ = enabled;
    }

    bool StatusBoard::isEnabled(void) const noexcept
    {
        return this->enabled_;
    }

    bool StatusBoard::isInteractive(void) const noexcept
    {
        return this->enabled_ && stdoutIsTerminal() && terminalSupportsAnsi();
    }

    bool StatusBoard::colorSupported(void) noexcept
    {
        // 仅在输出目标确为终端时着色 (与 spdlog ansicolor automatic 模式一致);
        // 重定向到文件/管道时输出纯文本, 避免日志文件被转义序列污染
        return stdoutIsTerminal() && colorEnabled();
    }

    void StatusBoard::update(const ::std::string& key, ::std::string value, StatusLevel level)
    {
        ::std::lock_guard lock { this->mutex_ };
        Item&             item { this->findOrCreateLocked(key) };
        if (item.value == value && item.level == level)
        {
            return; // 值未变化, 不产生任何输出
        }
        item.value = ::std::move(value);
        item.level = level;
        if (this->interactiveLocked())
        {
            this->eraseBlockLocked();
            this->drawBlockLocked();
            this->flushLocked();
        }
        else if (this->enabled_)
        {
            this->writePlainLocked("[状态] " + key + " = " + item.value);
        }
    }

    void StatusBoard::log(::std::string_view line)
    {
        ::std::lock_guard lock { this->mutex_ };
        if (!this->interactiveLocked())
        {
            this->writePlainLocked(::std::string { line });
            return;
        }
        this->eraseBlockLocked();
        this->writeLogLocked(line);
        this->drawBlockLocked();
        this->flushLocked();
    }

    void StatusBoard::finish(void) noexcept
    {
        ::std::lock_guard lock { this->mutex_ };
        if (this->interactiveLocked())
        {
            this->eraseBlockLocked();
        }
        this->flushLocked();
    }

    bool StatusBoard::interactiveLocked(void) const noexcept
    {
        return this->enabled_ && stdoutIsTerminal() && terminalSupportsAnsi();
    }

    StatusBoard::Item& StatusBoard::findOrCreateLocked(const ::std::string& key)
    {
        auto it { ::std::find_if(
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
        if (this->buffer_.empty())
        {
            return;
        }
        // write 可能被信号中断 (EINTR) 或短写: 循环写尽, 避免丢失输出。
        // 失败分支 (EINTR/EPIPE) 属运行时防御路径, 单测无法注入, 有意保留
        size_t offset { 0 };
        while (offset < this->buffer_.size())
        {
            const ::ssize_t written { ::write(STDOUT_FILENO, this->buffer_.data() + offset, this->buffer_.size() - offset) };
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
        this->buffer_.clear();
    }

    void StatusBoard::writePlainLocked(const ::std::string& line)
    {
        // 剥离调用方可能自带的行尾, 统一追加单个换行 (避免重定向到文件时出现空行)
        ::std::string_view view { line };
        while (!view.empty() && (view.back() == '\n' || view.back() == '\r'))
        {
            view.remove_suffix(1);
        }
        this->buffer_.append(view);
        this->buffer_.push_back('\n');
        this->flushLocked();
    }

    void StatusBoard::writeLogLocked(::std::string_view text)
    {
        for (const char c : text)
        {
            if (c == '\n')
            {
                this->buffer_.append("\r\n");
            }
            else
            {
                this->buffer_.push_back(c);
            }
        }
        if (text.empty() || text.back() != '\n')
        {
            this->buffer_.append("\r\n");
        }
    }

    // 状态块当前位于"已输出内容末尾", 光标停在块最后一行末尾 (无尾换行)。
    // 因此上移 (行数-1) 行即可回到块首, 再用 \033[J 清除到屏尾, 即完整抹除。
    void StatusBoard::eraseBlockLocked(void)
    {
        if (this->drawn_lines_ <= 0)
        {
            return;
        }
        if (this->drawn_lines_ > 1)
        {
            this->buffer_.append("\033[");
            this->buffer_.append(::std::to_string(this->drawn_lines_ - 1));
            this->buffer_.append("A");
        }
        this->buffer_.append("\r\033[J");
        this->drawn_lines_ = 0;
    }

    // 状态块布局: 每行 2~3 项; 列起点固定为终端宽度的等分位置 (行首 / 1/2; 或 1/3, 2/3),
    // 不随内容长度漂移; 放不下时降级 3 列 -> 2 列 -> 1 列。
    void StatusBoard::drawBlockLocked(void)
    {
        if (this->items_.empty())
        {
            return;
        }
        // 宽度探测失败 (0x0 pty / 无窗口尺寸的串口终端) 时按 80 列保守布局, 防止折行破坏块结构
        constexpr int FALLBACK_WIDTH { 80 };
        const int     measuredWidth { terminalWidth() };
        const int     width { measuredWidth > 0 ? measuredWidth : FALLBACK_WIDTH };
        const bool    colors { StatusBoard::colorSupported() };
        constexpr int SEPARATOR_LINES { 2 }; // 状态块与日志区之间的空行数

        // 各项的纯文本与最大宽度 (用于决定列数)
        ::std::vector<::std::string> plains;
        plains.reserve(this->items_.size());
        int maxItemWidth { 0 };
        for (const Item& item : this->items_)
        {
            plains.push_back(item.key + " : " + item.value);
            maxItemWidth = ::std::max(maxItemWidth, displayWidth(plains.back()));
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

        this->buffer_.append("\r");
        // 与日志区之间留空行; 空行跟随状态块一起抹除/重绘, 计入 drawn_lines_
        for (int i = 0; i < SEPARATOR_LINES; ++i)
        {
            this->buffer_.append("\r\n");
        }
        int lines { 0 };
        for (size_t i = 0; i < this->items_.size(); i += static_cast<size_t>(columns))
        {
            const size_t rowEnd { ::std::min(this->items_.size(), i + static_cast<size_t>(columns)) };
            int          cursorPos { 0 }; // 本行已输出到的显示列位置
            for (size_t j = i; j < rowEnd; ++j)
            {
                const size_t col { j - i };
                const bool   lastInRow { j + 1 == rowEnd };

                // 本格起点: 终端宽度的固定等分位置
                const int startPos { static_cast<int>(static_cast<long long>(width) * static_cast<int>(col) / columns) };
                const int endPos { lastInRow ? width : static_cast<int>(static_cast<long long>(width) * (static_cast<int>(col) + 1) / columns) };
                const int avail { ::std::max(endPos - startPos - 1, 1) }; // 预留 1 列间隔

                const int pad { startPos - cursorPos };
                if (pad > 0)
                {
                    this->buffer_.append(static_cast<size_t>(pad), ' ');
                }

                const Item&          item { this->items_[j] };
                const ::std::string& plain { plains[j] };
                ::std::string        text { plain };
                if (displayWidth(text) > avail)
                {
                    text = truncateToWidth(text, avail); // 超宽截断 (放弃着色)
                }

                if (colors && text == plain)
                {
                    this->buffer_.append("\033[36m");
                    this->buffer_.append(item.key);
                    this->buffer_.append("\033[0m : ");
                    this->buffer_.append(levelColor(item.level));
                    this->buffer_.append(item.value);
                    this->buffer_.append("\033[0m");
                }
                else
                {
                    this->buffer_.append(text);
                }
                cursorPos = startPos + displayWidth(text);
            }
            ++lines;
            if (rowEnd < this->items_.size())
            {
                this->buffer_.append("\r\n");
            }
        }
        // 末行不加换行: 光标停在状态块最后一行, 供下次 eraseBlockLocked 定位
        drawn_lines_ = lines + SEPARATOR_LINES;
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

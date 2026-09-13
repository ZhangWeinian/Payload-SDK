// cy_psdk/tests/test_status_board.cpp
//
// StatusBoard 单元测试: 通过 PTY / 管道重定向 STDOUT_FILENO 覆盖交互与非交互两条路径。
//   - PTY 模式: 激活 isatty 分支 (状态块绘制/擦除/配色/列布局/截断)
//   - 管道模式: 覆盖非交互降级 (普通输出) 与禁用行为
//   - StatusBoardSink: spdlog 接收器的纯文本/着色两条输出路径与级别配色

#include "utils/log_util/StatusBoardSink.h"
#include "utils/status_board/StatusBoard.h"

#include <gtest/gtest.h>

#include <spdlog/details/log_msg.h>

#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <termios.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using plane::utils::StatusBoard;
    using plane::utils::StatusBoardSink;
    using plane::utils::StatusLevel;

    // 环境变量设置/恢复
    class EnvGuard
    {
    public:
        EnvGuard(const char* name, const char* value): name_(name)
        {
            const char* old { ::getenv(name) };
            if (old != nullptr)
            {
                old_ = old;
                had_ = true;
            }
            if (value != nullptr)
            {
                ::setenv(name, value, 1);
            }
            else
            {
                ::unsetenv(name);
            }
        }

        ~EnvGuard()
        {
            if (had_)
            {
                ::setenv(name_.c_str(), old_.c_str(), 1);
            }
            else
            {
                ::unsetenv(name_.c_str());
            }
        }

        EnvGuard(const EnvGuard&)            = delete;
        EnvGuard& operator=(const EnvGuard&) = delete;

    private:
        ::std::string name_ {};
        ::std::string old_ {};
        bool          had_ { false };
    };

    // 把 STDOUT_FILENO 重定向到 PTY (isatty=true) 或管道 (isatty=false), 并读回输出
    class ScopedTerminal
    {
    public:
        enum class Mode
        {
            Pipe, // 非交互: isatty=false
            Pty   // 交互: isatty=true
        };

        explicit ScopedTerminal(Mode mode, int cols = 0)
        {
            saved_ = ::dup(STDOUT_FILENO);
            if (mode == Mode::Pty)
            {
                const struct winsize size { .ws_row = 24, .ws_col = static_cast<unsigned short>(cols), .ws_xpixel = 0, .ws_ypixel = 0 };
                int                  slave { -1 };
                if (::openpty(&master_, &slave, nullptr, nullptr, cols > 0 ? &size : nullptr) != 0)
                {
                    return;
                }
                // 关闭 PTY 输出后处理 (OPOST/ONLCR), 保证读到的是原始字节流
                struct termios attr {};
                if (::tcgetattr(slave, &attr) == 0)
                {
                    attr.c_oflag &= static_cast<::tcflag_t>(~OPOST);
                    ::tcsetattr(slave, TCSANOW, &attr);
                }
                ::dup2(slave, STDOUT_FILENO);
                ::close(slave);
            }
            else
            {
                int fds[2] { -1, -1 };
                if (::pipe(fds) != 0)
                {
                    return;
                }
                read_fd_ = fds[0];
                ::dup2(fds[1], STDOUT_FILENO);
                ::close(fds[1]);
            }
        }

        ~ScopedTerminal()
        {
            ::dup2(saved_, STDOUT_FILENO);
            ::close(saved_);
            if (master_ >= 0)
            {
                ::close(master_);
            }
            if (read_fd_ >= 0)
            {
                ::close(read_fd_);
            }
        }

        ScopedTerminal(const ScopedTerminal&)            = delete;
        ScopedTerminal& operator=(const ScopedTerminal&) = delete;

        // 读尽当前已写出的输出 (50ms 窗口)
        [[nodiscard]] ::std::string read(void)
        {
            const int fd { master_ >= 0 ? master_ : read_fd_ };
            ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);
            ::std::string out;
            char          buffer[4096];
            for (;;)
            {
                struct pollfd pfd { .fd = fd, .events = POLLIN, .revents = 0 };
                if (::poll(&pfd, 1, 50) <= 0)
                {
                    break;
                }
                const ::ssize_t n { ::read(fd, buffer, sizeof(buffer)) };
                if (n <= 0)
                {
                    break;
                }
                out.append(buffer, static_cast<::std::size_t>(n));
            }
            return out;
        }

    private:
        int saved_ { -1 };
        int master_ { -1 };
        int read_fd_ { -1 };
    };

    constexpr const char* kTerm { "xterm-256color" };

    // 断言发生期间 stdout 被重定向, gtest 的失败详情会写进被捕获的终端而丢失;
    // 失败时把实际字节以转义形式渲染到 stderr, 保证诊断可见
    [[nodiscard]] ::std::string visible(::std::string_view text)
    {
        ::std::string out;
        for (const char c : text)
        {
            if (c == '\033')
            {
                out += "\\e";
            }
            else if (c == '\r')
            {
                out += "\\r";
            }
            else if (c == '\n')
            {
                out += "\\n";
            }
            else if (static_cast<unsigned char>(c) < 0X20)
            {
                char buffer[8] {};
                ::snprintf(buffer, sizeof(buffer), "\\x%02x", static_cast<unsigned char>(c));
                out += buffer;
            }
            else
            {
                out.push_back(c);
            }
        }
        return out;
    }

    void expectBytes(const char* tag, const ::std::string& actual, const ::std::string& expected)
    {
        if (actual != expected)
        {
            ::dprintf(STDERR_FILENO, "[%s] ACTUAL = %s\n[%s] EXPECT = %s\n", tag, visible(actual).c_str(), tag, visible(expected).c_str());
        }
        EXPECT_EQ(actual, expected);
    }

    void expectContains(const char* tag, const ::std::string& actual, ::std::string_view needle)
    {
        if (actual.find(needle) == ::std::string::npos)
        {
            ::dprintf(STDERR_FILENO, "[%s] ACTUAL = %s\n", tag, visible(actual).c_str());
        }
        EXPECT_NE(actual.find(needle), ::std::string::npos);
    }
} // namespace

// ---- 交互 (PTY) 模式 ----

// 空状态板下仅透传日志: erase 无操作, drawBlock 因无条目直接返回
TEST(StatusBoardTest, LogBeforeFirstUpdatePassesThrough)
{
    EnvGuard       term { "TERM", kTerm };
    EnvGuard       nocolor { "NO_COLOR", nullptr };
    ScopedTerminal capture { ScopedTerminal::Mode::Pty };

    StatusBoard&   board { StatusBoard::getInstance() };
    board.log("boot-up"); // 单行: 不追加额外换行
    board.log("l1\nl2");  // 多行: 内部 \n -> \r\n
    board.log("end\n");   // 自带行尾: 不重复追加
    board.log("");        // 空串: 仅输出一个 \r\n

    const ::std::string delta { capture.read() };

    EXPECT_EQ(delta, "boot-up\r\nl1\r\nl2\r\nend\r\n\r\n");
}

// 首次 update: 绘制状态块 (回退宽度 80, 3 列布局, 着色)
TEST(StatusBoardTest, FirstUpdateDrawsColoredBlock)
{
    EnvGuard       term { "TERM", kTerm };
    EnvGuard       nocolor { "NO_COLOR", nullptr };
    ScopedTerminal capture { ScopedTerminal::Mode::Pty }; // 不设窗口尺寸 -> 回退 80 列

    StatusBoard&   board { StatusBoard::getInstance() };
    board.update("svc", "UP", StatusLevel::Ok);
    const ::std::string first { capture.read() };

    // 块首: \r + 两个分隔空行 + 单元格 (键青色 / 值按级别)
    EXPECT_EQ(first, "\r\r\n\r\n\033[36msvc\033[0m : \033[32mUP\033[0m");

    // 第二次 update: 抹除 3 行块 (\033[2A\r\033[J) 后重绘 2 项
    board.update("mode", "GUIDED", StatusLevel::Warn);
    const ::std::string second { capture.read() };

    EXPECT_EQ(
        second,
        "\033[2A\r\033[J\r\r\n\r\n\033[36msvc\033[0m : \033[32mUP\033[0m" + ::std::string(18, ' ') +
            "\033[36mmode\033[0m : \033[33mGUIDED\033[0m"
    );

    // 第三/四项: Info 无配色码, Error 红色
    board.update("info", "ready", StatusLevel::Info);
    board.update("fail", "E1", StatusLevel::Error);
    const ::std::string third { capture.read() };

    EXPECT_NE(third.find("\033[36minfo\033[0m : ready\033[0m"), ::std::string::npos);
    EXPECT_NE(third.find("\033[36mfail\033[0m : \033[31mE1\033[0m"), ::std::string::npos);
}

// 值 (含级别) 未变化时不产生任何输出; 级别变化触发重绘
TEST(StatusBoardTest, UnchangedUpdateProducesNothing)
{
    EnvGuard       term { "TERM", kTerm };
    EnvGuard       nocolor { "NO_COLOR", nullptr };
    ScopedTerminal capture { ScopedTerminal::Mode::Pty };

    StatusBoard&   board { StatusBoard::getInstance() };
    board.update("svc", "UP", StatusLevel::Ok);
    const ::std::string noop { capture.read() };
    EXPECT_TRUE(noop.empty());

    board.update("svc", "UP", StatusLevel::Warn); // 值同级别异 -> 重绘
    const ::std::string changed { capture.read() };
    EXPECT_NE(changed.find("\033[33mUP\033[0m"), ::std::string::npos);

    board.update("svc", "UP", StatusLevel::Ok); // 复原
    (void)capture.read();                       // 排空, 保持后续测试的增量输出干净
}

// 窄终端降级为单列: 每项独占一行 (\r\n 分隔); 无色模式下单元格为纯文本
TEST(StatusBoardTest, NarrowTerminalDegradesToSingleColumn)
{
    EnvGuard       term { "TERM", kTerm };
    EnvGuard       nocolor { "NO_COLOR", "1" }; // 交互但不着色
    ScopedTerminal capture { ScopedTerminal::Mode::Pty, 24 };

    StatusBoard&   board { StatusBoard::getInstance() };
    board.log("marker");
    const ::std::string delta { capture.read() };

    const auto          first { delta.find("svc : UP") };
    const auto          second { delta.find("mode : GUIDED") };
    ASSERT_NE(first, ::std::string::npos);
    ASSERT_NE(second, ::std::string::npos);
    // 单列布局: 两项之间以 \r\n 换行分隔 (而非同行的等分定位空格)
    EXPECT_LT(first, second);
    EXPECT_NE(delta.find("\r\n", first), ::std::string::npos);
    EXPECT_NE(delta.find("marker"), ::std::string::npos);
}

// 超宽项截断: 追加省略号且放弃着色, 多字节字符不被拆断
TEST(StatusBoardTest, OverlongItemIsTruncatedWithEllipsis)
{
    EnvGuard       term { "TERM", kTerm };
    EnvGuard       nocolor { "NO_COLOR", nullptr };
    ScopedTerminal capture { ScopedTerminal::Mode::Pty, 20 };

    StatusBoard&   board { StatusBoard::getInstance() };
    board.update("long", ::std::string(30, 'x'), StatusLevel::Info); // 先占位 (ASCII, 稍后被替换)
    (void)capture.read();                                            // 丢弃首次绘制

    board.update("long", ::std::string {} + "汉" + "汉" + "汉" + "汉" + "汉" + "汉" + "汉" + "汉", StatusLevel::Info);
    const ::std::string delta { capture.read() };

    EXPECT_NE(delta.find("long : 汉汉汉汉汉…"), ::std::string::npos); // 19 列内 5 个双宽字符 + 省略号
    EXPECT_EQ(delta.find("\033[36mlong"), ::std::string::npos);       // 截断单元格放弃着色
}

// 2/4 字节 UTF-8: 双字节字符按 1 列、四字节字符按 2 列估算; 截断不拆断码点
TEST(StatusBoardTest, TwoAndFourByteUtf8Widths)
{
    EnvGuard       term { "TERM", kTerm };
    EnvGuard       nocolor { "NO_COLOR", nullptr };
    ScopedTerminal capture { ScopedTerminal::Mode::Pty, 20 };

    const auto     repeat = [](const char* unit, int count)
    {
        ::std::string out;
        for (int i = 0; i < count; ++i)
        {
            out += unit;
        }
        return out;
    };

    StatusBoard& board { StatusBoard::getInstance() };
    // 16 个双字节字符 = 16 列: "e2 : "(5) + 16 > 19 列配额, 截到 18 列 (13 个) + 省略号
    board.update("e2", repeat("é", 16), StatusLevel::Info);
    expectContains("u8-2byte", capture.read(), "e2 : " + repeat("é", 13) + "…");

    // 10 个四字节字符 = 20 列: 截到 17 列 (6 个) + 省略号
    board.update("e4", repeat("😀", 10), StatusLevel::Info);
    const ::std::string delta { capture.read() };
    expectContains("u8-4byte", delta, "e4 : " + repeat("😀", 6) + "…");
    EXPECT_EQ(delta.find("\033[36me4"), ::std::string::npos); // 截断放弃着色
}

// finish: 抹除已绘制的状态块; 重复调用无副作用
TEST(StatusBoardTest, FinishErasesDrawnBlockOnce)
{
    EnvGuard       term { "TERM", kTerm };
    EnvGuard       nocolor { "NO_COLOR", nullptr };
    ScopedTerminal capture { ScopedTerminal::Mode::Pty };

    StatusBoard&   board { StatusBoard::getInstance() };
    board.finish();
    const ::std::string first { capture.read() };
    EXPECT_NE(first.find("\033[J"), ::std::string::npos); // 已抹除

    board.finish();
    const ::std::string second { capture.read() };
    EXPECT_TRUE(second.empty()); // 无块可抹 -> 无输出
}

// TERM=dumb: 交互判定失败 -> 状态更新退化为普通输出
// NO_COLOR: 交互但不着色 -> 绘制不含转义序列
// 禁用 (setEnabled(false)): 状态更新静默, 日志仍透传
TEST(StatusBoardTest, EnvironmentAndEnabledSwitches)
{
    StatusBoard& board { StatusBoard::getInstance() };

    {
        EnvGuard       term { "TERM", "dumb" };
        ScopedTerminal capture { ScopedTerminal::Mode::Pty };

        board.update("dumbk", "dv");
        expectBytes("dumb", capture.read(), "[状态] dumbk = dv\n");
    }
    {
        EnvGuard       term { "TERM", kTerm };
        EnvGuard       nocolor { "NO_COLOR", "1" };
        ScopedTerminal capture { ScopedTerminal::Mode::Pty, 120 };

        board.update("nck", "nv");
        const ::std::string delta { capture.read() };
        // NO_COLOR 下重绘为纯文本: 整段增量不含任何转义序列
        if (delta.find('\033') != ::std::string::npos)
        {
            ::dprintf(STDERR_FILENO, "[nocolor] ACTUAL = %s\n", visible(delta).c_str());
        }
        EXPECT_EQ(delta.find('\033'), ::std::string::npos);
        expectContains("nocolor", delta, "dumbk : dv"); // 前一段遗留项一并重绘
        expectContains("nocolor", delta, "nck : nv");
    }
    {
        ScopedTerminal capture { ScopedTerminal::Mode::Pty, 120 };
        EXPECT_TRUE(board.isEnabled()); // 禁用前默认启用

        board.setEnabled(false);
        EXPECT_FALSE(board.isEnabled());
        EXPECT_FALSE(board.isInteractive());
        board.update("noop", "x"); // 禁用: 状态更新静默
        board.log("still");        // 日志不受禁用影响
        expectBytes("disabled", capture.read(), "still\n");

        board.setEnabled(true);
    }
}

// ---- 非交互 (管道) 模式 ----

TEST(StatusBoardTest, PlainModeOnPipe)
{
    EnvGuard       term { "TERM", kTerm };
    ScopedTerminal capture { ScopedTerminal::Mode::Pipe };

    StatusBoard&   board { StatusBoard::getInstance() };
    EXPECT_FALSE(board.isInteractive());
    EXPECT_FALSE(StatusBoard::colorSupported());

    board.update("plainkey", "PV");
    board.log("raw\n\n");     // 行尾被剥离, 统一单换行
    board.log("multi\nline"); // 内部换行保留
    board.log("cr\r");        // \r 同样剥离
    board.finish();           // 非交互: 无抹除、无残留输出

    const ::std::string delta { capture.read() };
    EXPECT_EQ(delta, "[状态] plainkey = PV\nraw\nmulti\nline\ncr\n");
}

TEST(StatusBoardTest, DisabledSuppressesUpdatesButNotLogs)
{
    ScopedTerminal capture { ScopedTerminal::Mode::Pipe };
    StatusBoard&   board { StatusBoard::getInstance() };

    board.setEnabled(false);
    board.update("noop2", "x"); // 禁用: 静默但仍记录值
    board.log("still2");        // 日志仍输出
    const ::std::string disabled { capture.read() };

    board.setEnabled(true);
    board.update("noop2", "y"); // 恢复后: 普通输出 (值不同才触发)
    const ::std::string enabled { capture.read() };

    expectBytes("disabled", disabled, "still2\n");
    expectBytes("reenable", enabled, "[状态] noop2 = y\n");
}

// ---- StatusBoardSink (spdlog 接收器) ----

TEST(StatusBoardSinkTest, PlainPassthroughAndFlush)
{
    ScopedTerminal  capture { ScopedTerminal::Mode::Pipe };

    StatusBoardSink sink {};
    sink.set_pattern("%v");
    sink.log(::spdlog::details::log_msg { "test", ::spdlog::level::info, "sinkline" });
    sink.flush();
    const ::std::string delta { capture.read() };

    EXPECT_EQ(delta, "sinkline\n");
}

TEST(StatusBoardSinkTest, ColoredPayloadWithLevelCodes)
{
    EnvGuard        term { "TERM", kTerm };
    EnvGuard        nocolor { "NO_COLOR", nullptr };
    ScopedTerminal  capture { ScopedTerminal::Mode::Pty };

    StatusBoardSink sink {};
    sink.set_pattern("%^%v%$"); // %^..%$ 标记区间 -> 触发着色分支

    const ::std::vector<::std::pair<::spdlog::level::level_enum, ::std::string>> cases {
        { ::spdlog::level::trace,    "\033[37m"   }, // 白
        { ::spdlog::level::debug,    "\033[36m"   }, // 青
        { ::spdlog::level::info,     "\033[32m"   }, // 绿
        { ::spdlog::level::warn,     "\033[33m"   }, // 黄
        { ::spdlog::level::err,      "\033[31m"   }, // 红
        { ::spdlog::level::critical, "\033[1;31m" }, // 粗红
        { ::spdlog::level::off,      ""           }  // 无颜色码
    };
    for (const auto& [level, code] : cases)
    {
        sink.log(::spdlog::details::log_msg { "test", level, "colored" });
        const ::std::string delta { capture.read() };
        EXPECT_NE(delta.find(code + "colored\033[0m"), ::std::string::npos);
    }
}

TEST(StatusBoardSinkTest, ColoredButNoRangeFallsBackToPlain)
{
    EnvGuard        term { "TERM", kTerm };
    EnvGuard        nocolor { "NO_COLOR", nullptr };
    ScopedTerminal  capture { ScopedTerminal::Mode::Pty };

    StatusBoardSink sink {};
    sink.set_pattern("%v"); // 无 %^..%$ -> color_range 为空 -> 纯文本拼接
    sink.log(::spdlog::details::log_msg { "test", ::spdlog::level::info, "range-free" });
    const ::std::string delta { capture.read() };

    // log 路径将行尾统一为 \r\n
    expectContains("no-range", delta, "range-free\r\n");
    EXPECT_EQ(delta.find("\033[32mrange-free"), ::std::string::npos);
}

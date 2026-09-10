// cy_psdk/tools/status_board_demo/status_board_demo.cpp
//
// 终端状态板 PoC: 固定状态块 (连接状态等) + 正常滚动日志。
//
// 痛点: MQTT / WebSocket 等连接状态若通过普通日志输出, 会被业务日志淹没, 不利于观察。
// 方案: 状态块始终跟随在"已输出内容"的末尾 (内容超过一屏时即固定在终端底部);
//       每次输出日志时先抹除状态块 -> 打印日志 -> 重绘状态块。
//       日志正常向上滚动并保留在 scrollback 中, 状态则始终可见。
//
// 特性:
//   - 零第三方依赖 (POSIX + ANSI 转义, 3588 板端可直接使用)
//   - 非交互终端 (重定向 / systemd / TERM=dumb) 自动降级为普通输出
//   - 状态值未变化时不产生任何输出; 值按级别着色 (尊重 NO_COLOR)
//   - 退出时抹除状态块, 不留残影
//
// 编译: g++ -std=c++23 -O2 -Wall -Wextra status_board_demo.cpp -o status_board_demo
// 运行: ./status_board_demo [--no-board] [--seconds N]   (Ctrl+C 可提前退出)

#include <string_view>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <sys/ioctl.h>
#include <unistd.h>

namespace demo
{
	using namespace std::chrono_literals;

	// ------------------------------------------------------------ 终端环境探测
	[[nodiscard]] inline bool stdoutIsTerminal() noexcept
	{
		return ::isatty(STDOUT_FILENO) != 0;
	}

	[[nodiscard]] inline bool terminalSupportsAnsi() noexcept
	{
		const char* term { ::getenv("TERM") };
		return term != nullptr && std::strcmp(term, "dumb") != 0;
	}

	[[nodiscard]] inline bool colorEnabled() noexcept
	{
		return ::getenv("NO_COLOR") == nullptr && terminalSupportsAnsi();
	}

	[[nodiscard]] inline int terminalWidth() noexcept
	{
		struct winsize ws {};
		if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
		{
			return static_cast<int>(ws.ws_col);
		}
		return 0; // 未知宽度, 不做截断
	}

	// UTF-8 近似显示宽度: ASCII 1 列; 双字节码点 1 列; 三/四字节码点 (CJK/emoji) 2 列。
	// 注意: 这是保守估算, 宁可略宽也不让状态行折行 (折行会破坏状态块的固定行数)。
	[[nodiscard]] inline int displayWidth(std::string_view text) noexcept
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
			i	  += std::min(len, text.size() - i);
		}
		return width;
	}

	[[nodiscard]] inline std::string truncateToWidth(std::string_view text, int maxWidth)
	{
		if (maxWidth <= 0 || displayWidth(text) <= maxWidth)
		{
			return std::string { text };
		}
		std::string out;
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
			len = std::min(len, text.size() - i);
			const int w { (b < 0X80) ? 1 : ((len == 2) ? 1 : 2) };
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

	// ------------------------------------------------------------ 状态级别
	enum class Level
	{
		Info, // 默认前景色
		Ok,	  // 绿色
		Warn, // 黄色
		Error // 红色
	};

	// ------------------------------------------------------------ 状态板
	class StatusBoard
	{
	public:
		static StatusBoard& instance()
		{
			static StatusBoard board;
			return board;
		}

		// 手动禁用状态板 (此后只输出普通日志)
		void disable() noexcept
		{
			enabled_ = false;
		}

		[[nodiscard]] bool interactive() const noexcept
		{
			return enabled_ && stdoutIsTerminal() && terminalSupportsAnsi();
		}

		// 更新一项状态; 值 (含级别) 未变化时不产生任何输出
		void update(const std::string& key, std::string value, Level level = Level::Info)
		{
			std::lock_guard lock { mutex_ };
			Item&			item { this->findOrCreateLocked(key) };
			if (item.value == value && item.level == level)
			{
				return;
			}
			item.value = std::move(value);
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

		// 输出一行 (或多行) 日志: 抹除状态块 -> 打印日志 -> 重绘状态块
		void log(std::string_view line)
		{
			std::lock_guard lock { mutex_ };
			if (!this->interactiveLocked())
			{
				this->writePlainLocked(std::string { line });
				return;
			}
			this->eraseBlockLocked();
			this->writeLogLocked(line);
			this->drawBlockLocked();
			this->flushLocked();
		}

		// 退出前调用: 抹除状态块, 不留残影
		void finish()
		{
			std::lock_guard lock { mutex_ };
			if (this->interactiveLocked())
			{
				this->eraseBlockLocked();
			}
			this->flushLocked();
		}

	private:
		struct Item
		{
			std::string key;
			std::string value;
			Level		level { Level::Info };
		};

		// -------------------------------------------------------- 内部操作 (均需持锁)
		[[nodiscard]] bool interactiveLocked() const noexcept
		{
			return enabled_ && stdoutIsTerminal() && terminalSupportsAnsi();
		}

		Item& findOrCreateLocked(const std::string& key)
		{
			auto it { std::find_if(
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
			items_.push_back(Item { .key = key, .value = {}, .level = Level::Info });
			return items_.back();
		}

		void flushLocked()
		{
			if (!buffer_.empty())
			{
				::write(STDOUT_FILENO, buffer_.data(), buffer_.size());
				buffer_.clear();
			}
		}

		void writePlainLocked(const std::string& line)
		{
			buffer_.append(line);
			buffer_.append("\n");
			this->flushLocked();
		}

		void writeLogLocked(std::string_view text)
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
		void eraseBlockLocked()
		{
			if (drawn_lines_ <= 0)
			{
				return;
			}
			if (drawn_lines_ > 1)
			{
				buffer_.append("\033[");
				buffer_.append(std::to_string(drawn_lines_ - 1));
				buffer_.append("A");
			}
			buffer_.append("\r\033[J");
			drawn_lines_ = 0;
		}

		void drawBlockLocked()
		{
			if (items_.empty())
			{
				return;
			}
			const int  width { terminalWidth() };
			const bool colors { colorEnabled() };

			buffer_.append("\r");
			for (size_t i = 0; i < items_.size(); ++i)
			{
				const Item&		  item { items_[i] };
				const std::string plain { item.key + " : " + item.value };
				std::string		  row;
				if (width > 0 && displayWidth(plain) > width)
				{
					// 超宽: 截断输出 (仅极窄终端或超长值会走到这里), 放弃着色保持简单
					row = truncateToWidth(plain, width);
				}
				else if (colors)
				{
					row.append("\033[36m");
					row.append(item.key);
					row.append("\033[0m : ");
					row.append(levelColor(item.level));
					row.append(item.value);
					row.append("\033[0m");
				}
				else
				{
					row = plain;
				}
				buffer_.append(row);
				if (i + 1 < items_.size())
				{
					buffer_.append("\r\n");
				}
				// 末行不加换行: 光标停在状态块最后一行, 供下次 eraseBlockLocked 定位
			}
			drawn_lines_ = static_cast<int>(items_.size());
		}

		[[nodiscard]] static const char* levelColor(Level level) noexcept
		{
			switch (level)
			{
				case Level::Ok:
					return "\033[32m";
				case Level::Warn:
					return "\033[33m";
				case Level::Error:
					return "\033[31m";
				case Level::Info:
				default:
					return "";
			}
		}

		std::mutex		  mutex_;
		std::vector<Item> items_;			  // 顺序即显示顺序 (首次 update 的顺序)
		std::string		  buffer_;			  // 单次临界区内的输出缓冲, 一次 write 落盘
		int				  drawn_lines_ { 0 }; // 当前已绘制的状态块行数 (0 = 未绘制)
		std::atomic<bool> enabled_ { true };
	};

	// ------------------------------------------------------------ 演示程序
	namespace
	{
		std::atomic<bool> g_running { true };

		void			  onSigint(int /*sig*/)
		{
			g_running = false;
		}

		[[nodiscard]] std::string timestamp()
		{
			using namespace std::chrono;
			const auto		  now { system_clock::now() };
			const auto		  ms { duration_cast<milliseconds>(now.time_since_epoch()) % 1000 };
			const std::time_t tt { system_clock::to_time_t(now) };
			std::tm			  tm {};
			::localtime_r(&tt, &tm);
			char buf[32] {};
			std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d", tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms.count()));
			return buf;
		}

		// 注: 日志用 [时间] 前缀直接拼接, 不使用 std::format (避免演示对编译器格式化库版本的依赖)
		void runDemo(int seconds, bool board_enabled)
		{
			StatusBoard& board { StatusBoard::instance() };
			if (!board_enabled)
			{
				board.disable();
			}

			// 初始状态 (字段名与 cy_psdk 的实际状态一一对应)
			board.update("MQTT", "连接中…", Level::Warn);
			board.update("WebSocket", "未连接", Level::Warn);
			board.update("Catalog", "初始化中…", Level::Warn);
			board.update("Bind", "—");
			board.update("Serial", "—");

			board.log("=== 终端状态板演示: 上方为滚动日志, 下方固定块为实时状态 ===");
			board.log("提示: Ctrl+C 提前退出; 重定向输出或 --no-board 可观察降级行为");

			static const std::vector<std::string> logScript {
				"采集线程: 位置融合数据已更新",
				"飞控订阅: 电池 87% / 12.3V, 卫星 15 颗",
				"航线任务状态: EXECUTING (航点 3/10)",
				"激光测距轮询: 12.3 m",
				"HMS 健康状态: 无告警",
				"MQTT 心跳保活正常",
				"目录心跳: catalog_state=READY",
				"参数同步: 目标点已刷新",
				"遥测上报: 状态包已发布 (topic=/wrgk/uav/status)",
			};

			const auto start { std::chrono::steady_clock::now() };
			const int  total_ticks { seconds * 10 }; // 100ms 一拍

			bool	   mqtt_done { false };
			bool	   ws_done { false };
			bool	   catalog_done { false };
			bool	   ws_drop { false };
			bool	   ws_restore { false };

			for (int tick = 0; tick <= total_ticks && g_running; ++tick)
			{
				std::this_thread::sleep_for(100ms);
				const double t { tick / 10.0 };

				if (tick % 3 == 0 && tick > 0)
				{
					const size_t index { static_cast<size_t>(tick / 3) % logScript.size() };
					board.log("[" + timestamp() + "] " + logScript[index]);
				}

				if (!mqtt_done && t >= 1.2)
				{
					board.update("MQTT", "✔ 已连接 (tcp://127.0.0.1:1883)", Level::Ok);
					board.log("[" + timestamp() + "] MQTT 连接成功, 开始主题订阅");
					mqtt_done = true;
				}
				if (!ws_done && t >= 2.4)
				{
					board.update("WebSocket", "✔ 已连接 (ws://127.0.0.1:9002)", Level::Ok);
					board.log("[" + timestamp() + "] WebSocket 握手完成, 启动目录服务");
					ws_done = true;
				}
				if (!catalog_done && t >= 3.5)
				{
					board.update("Catalog", "READY", Level::Ok);
					board.update("Bind", "✔ 已绑定", Level::Ok);
					board.update("Serial", "1581F5BKD23A00XXXX");
					board.log("[" + timestamp() + "] 目录服务就绪, 广播设备上线");
					catalog_done = true;
				}
				if (!ws_drop && t >= 6.8)
				{
					board.update("WebSocket", "✘ 连接断开, 重连中…", Level::Error);
					board.log("[" + timestamp() + "] WebSocket 连接断开: 对端关闭 (code=1006)");
					ws_drop = true;
				}
				if (!ws_restore && t >= 8.4)
				{
					board.update("WebSocket", "✔ 已连接 (ws://127.0.0.1:9002)", Level::Ok);
					board.log("[" + timestamp() + "] WebSocket 重连成功");
					ws_restore = true;
				}
			}

			board.finish();
			std::string tail { "\n演示结束 (耗时 " };
			tail += std::
				to_string(static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count()));
			tail += " 秒)。\n";
			::write(STDOUT_FILENO, tail.data(), tail.size());
		}
	} // namespace
} // namespace demo

int main(int argc, char** argv)
{
	int	 seconds { 12 };
	bool board_enabled { true };

	for (int i = 1; i < argc; ++i)
	{
		const std::string_view arg { argv[i] };
		if (arg == "--no-board")
		{
			board_enabled = false;
		}
		else if (arg == "--seconds" && i + 1 < argc)
		{
			seconds = std::atoi(argv[++i]);
		}
		else
		{
			std::printf("用法: %s [--no-board] [--seconds N]\n", argv[0]);
			return 1;
		}
	}

	std::signal(SIGINT, demo::onSigint);
	demo::runDemo(seconds, board_enabled);
	return 0;
}

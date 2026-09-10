// cy_psdk/utils/status_board/StatusBoard.h
//
// 终端状态板: 在终端中维护一个"固定状态块", 展示连接状态等关键信息,
// 使其不被滚动日志淹没。状态块始终跟随在已输出内容的末尾 (内容满屏时即固定在
// 终端底部), 每次输出日志时自动让位 (抹除状态块 -> 打印日志 -> 重绘状态块)。
//
// 特性:
//   - 零第三方依赖 (POSIX + ANSI 转义)
//   - 非交互终端 (重定向 / systemd / TERM=dumb) 自动降级为普通输出
//   - 状态值未变化时不产生任何输出, 仅在变化时重绘
//   - 每行 2~3 项; 列起点固定为终端宽度的等分位置, 不随内容长度漂移
//
// 接入方式:
//   - 日志输出: Logger 的控制台接收器替换为 StatusBoardSink (见 log_util)
//   - 状态更新: plane::manager::StatusBoardManager 周期性同步 PlaneStateStore

#pragma once

#include <string_view>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "define.h"

namespace plane::utils
{
	// 状态级别: 决定状态值的显示颜色
	enum class StatusLevel
	{
		Info, // 默认前景色
		Ok,	  // 绿色
		Warn, // 黄色
		Error // 红色
	};

	// 终端状态板 (单例, 线程安全)
	class StatusBoard
	{
	public:
		static StatusBoard& getInstance(void) noexcept;

		// 启用/禁用状态板 (禁用后所有输出退化为普通模式)
		void setEnabled(bool enabled) noexcept;

		// 状态板是否启用 (不含终端能力判断)
		_NODISCARD bool isEnabled(void) const noexcept;

		// 是否运行于可交互终端 (否则自动降级: 状态变化以普通日志形式输出)
		_NODISCARD bool isInteractive(void) const noexcept;

		// 终端是否支持 ANSI 颜色 (isatty + TERM 非 dumb + 未设置 NO_COLOR)
		// 日志接收器 (StatusBoardSink) 的着色判断与此保持一致
		_NODISCARD static bool colorSupported(void) noexcept;

		// 更新一项状态; 值 (含级别) 未变化时不产生任何输出
		void update(const _STD string& key, _STD string value, StatusLevel level = StatusLevel::Info);

		// 输出一条日志 (可含换行): 抹除状态块 -> 打印日志 -> 重绘状态块
		// 非交互终端下行为与直接打印一致
		void log(_STD string_view line);

		// 退出前抹除状态块, 不在终端留下残影
		void finish(void) noexcept;

	private:
		StatusBoard(void) noexcept				   = default;
		~StatusBoard(void) noexcept				   = default;
		StatusBoard(const StatusBoard&)			   = delete;
		StatusBoard& operator=(const StatusBoard&) = delete;

		struct Item
		{
			_STD string key;
			_STD string value;
			StatusLevel level { StatusLevel::Info };
		};

		// ---- 内部操作 (均需持有 mutex_) ----
		_NODISCARD bool	   interactiveLocked(void) const noexcept;
		Item&			   findOrCreateLocked(const _STD string& key);
		void			   flushLocked(void);
		void			   writePlainLocked(const _STD string& line);
		void			   writeLogLocked(_STD string_view text);
		void			   eraseBlockLocked(void);
		void			   drawBlockLocked(void);
		static const char* levelColor(StatusLevel level) noexcept;

		_STD mutex		   mutex_ {};
		_STD vector<Item> items_ {};		  // 顺序即显示顺序 (首次 update 的顺序)
		_STD string		  buffer_ {};		  // 单次临界区内的输出缓冲, 一次 write 落盘
		int				  drawn_lines_ { 0 }; // 当前已绘制的状态块行数 (0 = 未绘制)
		_STD atomic<bool> enabled_ { true };
	};
} // namespace plane::utils

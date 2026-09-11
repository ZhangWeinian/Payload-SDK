#!/bin/sh
# 部署启动薄壳 (POSIX sh, busybox 兼容)。
#
# 设计: 目标板可能没有任何系统运行库/动态链接器, 因此所有运行库随交付目录 libs/ 打包。
# 这里优先用打包的解释器 + --library-path 显式启动 cy_psdk, 完全不依赖板端系统库;
# 若打包解释器不存在则回退到系统默认解释器。
# 完整性校验已内置在 cy_psdk 启动流程中 (SHA256, 无需 sha256sum 等外部工具)。

DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

# 强制 root 运行: I2C 硬件复位 / 串口权限 / sysfs GPIO 等硬件操作均需要 root 权限。
# 非 root 时自动通过 sudo 提权 (用户直接输入密码); 提权不可用则报错退出。
if [ "$(id -u)" -ne 0 ]; then
	if command -v sudo >/dev/null 2>&1; then
		echo "[提示] 需要 root 权限, 正在通过 sudo 提权 (请输入密码)..."
		exec sudo sh "$DIR/$(basename "$0")" "$@"
	fi
	echo "[错误] 本程序必须以 root 运行, 且未找到 sudo 无法自动提权" >&2
	echo "       请使用: sudo bash run.sh   或   su -c 'sh run.sh'" >&2
	exit 1
fi

# 修正传输/解压流程可能丢失的可执行位 (zip 解压 / Windows 共享 / 打包工具)。
# 动态链接器与主程序必须可执行, 否则会回退系统解释器, 可能因板端系统库过旧导致启动失败。
chmod +x "$DIR/cy_psdk" 2>/dev/null || true
chmod +x "$DIR"/libs/ld-linux-* 2>/dev/null || true

# 崩溃转储: 放开 core 大小限制, 并尽力让内核把 core 直接落到交付目录 dumps/。
#   - core_pattern 是系统级设置 (重启后恢复), 改动仅为方便收取本程序转储;
#   - 无权限或写入失败时静默跳过; 程序内还会对 RLIMIT_CORE 兜底。
ulimit -c unlimited 2>/dev/null || true
mkdir -p "$DIR/dumps" 2>/dev/null || true
if [ -w /proc/sys/kernel/core_pattern ]; then
	echo "$DIR/dumps/core.%t.%p" > /proc/sys/kernel/core_pattern 2>/dev/null || true
fi

# aarch64 / x86_64 打包解释器二选一
for LOADER in \
	"$DIR/libs/ld-linux-aarch64.so.1" \
	"$DIR/libs/ld-linux-x86-64.so.2"
do
	if [ -x "$LOADER" ]; then
		exec "$LOADER" --library-path "$DIR/libs" "$DIR/cy_psdk" "$@"
	fi
done

# 回退: 依赖系统默认解释器 (常规 Linux 环境)
exec "$DIR/cy_psdk" "$@"

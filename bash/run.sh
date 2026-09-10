#!/bin/sh
# 部署启动薄壳 (POSIX sh, busybox 兼容)。
#
# 设计: 目标板可能没有任何系统运行库/动态链接器, 因此所有运行库随交付目录 libs/ 打包。
# 这里优先用打包的解释器 + --library-path 显式启动 cy_psdk, 完全不依赖板端系统库;
# 若打包解释器不存在则回退到系统默认解释器。
# 完整性校验已内置在 cy_psdk 启动流程中 (SHA256, 无需 sha256sum 等外部工具)。

DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

# 修正传输/解压流程可能丢失的可执行位 (zip 解压 / Windows 共享 / 打包工具)。
# 动态链接器与主程序必须可执行, 否则会回退系统解释器, 可能因板端系统库过旧导致启动失败。
chmod +x "$DIR/cy_psdk" 2>/dev/null || true
chmod +x "$DIR"/libs/ld-linux-* 2>/dev/null || true

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

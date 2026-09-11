#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.9"
# dependencies = []
# ///
"""
cy_psdk 崩溃转储解析工具 (开发容器专用)

用法 (在仓库根目录):
    uv run tools/crash_report.py <core文件> [--exe cy_psdk路径] [--libs libs目录] [--out 报告路径]

示例:
    uv run tools/crash_report.py dumps/core.12345.1789100000 \
        --exe  build/aarch64-linux/release/bin/cy_psdk \
        --libs build/aarch64-linux/release/bin/libs

说明:
  - 依赖 gdb-multiarch (容器内安装: sudo apt-get install -y gdb-multiarch)
  - core 由板端 dumps/ 目录取回; 二进制与 libs 用同一次交付包里的文件, 保证符号匹配
  - 生成 <core文件名>.report.txt: 崩溃信号/故障地址/全部线程栈/寄存器/共享库/崩溃文本报告
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent


def find_gdb() -> str:
    for name in ("gdb-multiarch", "aarch64-linux-gnu-gdb", "gdb"):
        found = shutil.which(name)
        if found:
            return found
    sys.exit("[错误] 未找到 gdb; 请先安装: sudo apt-get install -y gdb-multiarch")


def find_exe(core: Path, explicit: Path | None) -> Path:
    if explicit is not None:
        if not explicit.exists():
            sys.exit(f"[错误] 指定的 exe 不存在: {explicit}")
        return explicit.resolve()

    candidates = [
        core.parent / "cy_psdk",  # core 在 dumps/ 时取上级也要看
        core.parent.parent / "cy_psdk",
        REPO_ROOT / "build" / "aarch64-linux" / "release" / "bin" / "cy_psdk",
        REPO_ROOT / "build" / "x86_64-linux" / "debug" / "bin" / "cy_psdk",
    ]
    for cand in candidates:
        if cand.exists():
            return cand.resolve()
    sys.exit("[错误] 未找到 cy_psdk; 请用 --exe 指定交付包中的二进制")


def find_libs(exe: Path, explicit: Path | None) -> Path | None:
    if explicit is not None:
        return explicit.resolve() if explicit.exists() else None
    cand = exe.parent / "libs"
    return cand.resolve() if cand.is_dir() else None


def find_crash_txt(core: Path) -> Path | None:
    for search_dir in (core.parent, core.parent / "dumps"):
        if search_dir.is_dir():
            reports = sorted(search_dir.glob("crash_*.txt"))
            if reports:
                return reports[-1]
    return None


def build_gdb_args(gdb: str, exe: Path, core: Path, libs: Path | None) -> list[str]:
    setup = [
        "set pagination off",
        "set confirm off",
        "set print frame-arguments none",
        "set backtrace limit 300",
        f"file {exe}",
    ]
    if libs is not None:
        setup.append(f"set solib-search-path {libs}")
    setup.append(f"core-file {core}")
    sections = [
        ("概要", ["info program"]),
        ("线程列表", ["info threads"]),
        ("全部线程调用栈", ["thread apply all bt"]),
        ("当前线程寄存器", ["info registers"]),
        ("共享库加载状态", ["info sharedlibrary"]),
    ]
    args = [gdb, "-q", "-batch"]
    for cmd in setup:
        args += ["-ex", cmd]
    for title, cmds in sections:
        args += ["-ex", f"echo \\n===== {title} =====\\n"]
        for cmd in cmds:
            args += ["-ex", cmd]
    return args


def main() -> None:
    ap = argparse.ArgumentParser(description="解析 cy_psdk core dump, 生成可读报告")
    ap.add_argument("core", type=Path, help="core 转储文件路径")
    ap.add_argument(
        "--exe", type=Path, default=None, help="cy_psdk 二进制 (默认自动查找)"
    )
    ap.add_argument(
        "--libs", type=Path, default=None, help="交付包 libs/ 目录 (默认 exe 同目录)"
    )
    ap.add_argument(
        "--out", type=Path, default=None, help="报告输出路径 (默认 <core>.report.txt)"
    )
    args = ap.parse_args()

    core = args.core.resolve()
    if not core.exists():
        sys.exit(f"[错误] core 文件不存在: {core}")

    gdb = find_gdb()
    exe = find_exe(core, args.exe)
    libs = find_libs(exe, args.libs)
    out = args.out if args.out is not None else Path(f"{core}.report.txt")

    print(f"[信息] core : {core} ({core.stat().st_size / 1024 / 1024:.1f} MB)")
    print(f"[信息] exe  : {exe}")
    print(f"[信息] libs : {libs if libs else '(未找到, 部分库帧可能无符号)'}")
    print(f"[信息] gdb  : {gdb}")

    proc = subprocess.run(
        build_gdb_args(gdb, exe, core, libs),
        capture_output=True,
        text=True,
        timeout=600,
        check=False,
    )
    gdb_out = proc.stdout + proc.stderr
    if proc.returncode != 0 and not gdb_out.strip():
        sys.exit(f"[错误] gdb 执行失败 (exit={proc.returncode})")

    crash_txt = find_crash_txt(core)
    parts: list[str] = []
    parts.append("=" * 60)
    parts.append("cy_psdk 崩溃报告")
    parts.append("=" * 60)
    parts.append(f"core : {core}")
    parts.append(f"exe  : {exe}")
    parts.append(f"libs : {libs if libs else '(未找到)'}")
    parts.append("")
    parts.append(gdb_out.strip())
    parts.append("")
    if crash_txt is not None:
        parts.append("=" * 60)
        parts.append(f"崩溃文本报告 (程序自写): {crash_txt.name}")
        parts.append("=" * 60)
        parts.append(crash_txt.read_text(encoding="utf-8", errors="replace").strip())
    else:
        parts.append("(未找到 crash_*.txt 文本报告)")

    report = "\n".join(parts) + "\n"
    out.write_text(report, encoding="utf-8")

    # ---- 终端摘要 ----
    for line in gdb_out.splitlines():
        if "Program terminated with signal" in line or line.startswith("#0 "):
            print(f"[摘要] {line.strip()}")
    print(f"[完成] 报告已写入: {out}")


if __name__ == "__main__":
    main()

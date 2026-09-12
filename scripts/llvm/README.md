# 双工具链开发流程 (LLVM 23 × GCC 16)

> 本目录 (`scripts/llvm/`) 集中存放双工具链的**开发辅助脚本**, 本文档为其使用说明。
> 构建/交付脚本保留在 `bash/` (`gen_checksum.sh`、`run.sh`), 二者职责分离。

本文档描述 cy_psdk 的**双工具链**工作流:

| 阶段 | 编译器 | 用途 |
| --- | --- | --- |
| 开发 / 测试 / 分析 (x86_64 容器) | **Clang/LLVM 23** | 编译、clang-tidy 静态分析、Sanitizers 动态分析、llvm-cov 覆盖率、llvm-mca 性能建模 |
| 部署产物 (aarch64 / RK3588S) | **GCC 16 交叉** | 生成最终交付二进制, 静态链接 libstdc++/libgcc 规避目标板运行库差异 |

核心思路: **x86 侧用 LLVM 全家桶快速迭代并回馈代码质量, aarch64 侧用 GCC 16 产出可部署产物**。

---

## 1. 环境要求 (已在开发容器验证)

| 工具 | 版本 / 路径 |
| --- | --- |
| clang / clang++ | 23.1.2 — `/usr/local/bin` |
| clang-tidy / clang-format / clangd | LLVM 23 — `/usr/local/bin` |
| llvm-cov / llvm-profdata / llvm-mca / llvm-objdump / llvm-bolt | LLVM 23 — `/usr/local/bin` |
| compiler-rt 运行库 (asan/ubsan/profile 等) | LLVM 23 — clang resource-dir `lib/linux/` |
| ld.lld | `/usr/local/bin/ld.lld` |
| g++-16 / aarch64-linux-gnu-g++-16 | 16.2.0 — `/usr/bin` |
| ccache | 4.11.2 |
| cmake / ninja | 4.4.3 / 1.13.2 |

> 全部 LLVM 工具已进入 `PATH`; compiler-rt 运行库随镜像提供,
> Sanitizer 与覆盖率均为 Clang 原生 runtime, 无需额外配置 (脚本中的
> `/usr/lib/llvm-23/bin` 回退探测仅作为兼容保留)。

依赖隔离: x86 Clang 构建使用 `vcpkg_installed-clang/`, aarch64 交叉使用 `vcpkg_installed/arm64/`,
两套依赖各自独立、互不污染。

### LLVM 工具使用地图

**已接入:**

| 工具 | 位置 |
| --- | --- |
| clang / clang++ 23 | `debug` / `tsan` / `cov` / `release` / `redbi` 全部 x86 预设 |
| ld.lld | 同上 (`CMAKE_LINKER_TYPE=LLD`, 含 LTO 链接) |
| compiler-rt (ASan/UBSan/TSan/profile) | `debug` / `tsan` / `cov` 预设运行时 |
| clang-tidy 23 | `scripts/llvm/clang-tidy.sh` + `.clang-tidy` (x86 / aarch64 架构感知双模式) |
| llvm-profdata + llvm-cov | `scripts/llvm/clang-coverage.sh` 源码级覆盖率 |
| llvm-mca (+ llvm-objdump / llvm-cxxfilt) | `scripts/llvm/llvm-mca.sh` aarch64 微架构建模 |
| llvm-symbolizer | PATH 提供, Sanitizer 栈回溯自动调用 |
| lldb-dap 23 (+ LLDB DAP 扩展) | `psdk/.vscode/launch.json`, F5 调试 debug 预设产物 (见 §8) |
| libFuzzer (compiler-rt) | `fuzz` 预设 + `fuzz/`, 目标: ProbePacketCodec / JsonCodec 链路 (见 §9) |

**已就绪、经评估未接入 (附原因):**

| 组件 | 原因 |
| --- | --- |
| llvm-ar / ranlib / nm / strip / objcopy | 工程无静态库构建 (`add_library` 为零), 无收益 |
| scan-build / scan-build-py | 与 clang-tidy 的 `clang-analyzer-*` 检查族重叠 |
| clangd | IntelliSense 目前由 cpptools 承担 (configurationProvider: cmake-tools) |
| MSan / libc++ | vcpkg 依赖为 libstdc++ ABI; 切换需重建全部依赖且偏离 aarch64 交付环境 |
| llvm-bolt / llvm-profgen | 仅适用于 LLVM 构建链; x86 产物不交付, aarch64 交付链为 GCC |
| llvm-exegesis | 需在目标机 (RK3588S) 上运行 |
| lifetime-safety (cc1 实验特性) | LLVM 23 仅 cc1 层暴露, 无稳定驱动开关 |

---

## 2. 预设一览

| 预设 | 编译器 | 用途 |
| --- | --- | --- |
| `debug` | Clang + ASan/UBSan | 日常开发与动态分析 (推荐默认) |
| `tsan` | Clang + TSan | 线程/数据竞争检测 (多线程流水线; 见 §4 容器限制) |
| `fuzz` | Clang + libFuzzer/ASan | 解析器模糊测试: ProbePacketCodec / JsonCodec (见 §9) |
| `cov` | Clang + 覆盖率插桩 | llvm-cov 覆盖率采集专用 |
| `release` | Clang (LTO / LLD) | 优化构建验证 + x86 性能基准 (含测试) |
| `redbi` | Clang | 带符号优化构建 |
| `aarch64` | GCC 16 交叉 | 交付产物 (静态 libstdc++/libgcc) |

```bash
# x86 开发 (Clang + ASan/UBSan)
cmake --preset debug
cmake --build --preset debug
ctest --preset debug

# x86 发布构建验证 (-O3 + LTO; 防止 debug 正常但 release 编译/运行失败)
cmake --preset release
cmake --build --preset release
ctest --preset release

# aarch64 交付 (GCC 16 交叉)
cmake --preset aarch64
cmake --build --preset aarch64
```

---

## 3. clang-tidy 静态分析

```bash
# x86 分析 (基于 debug 预设的编译数据库)
bash scripts/llvm/clang-tidy.sh

# 仅列出将被分析的文件
bash scripts/llvm/clang-tidy.sh --list

# 架构感知检查: 以 aarch64 为目标解析内置宏/ABI
#   (需先配置 aarch64 预设以生成交叉编译数据库)
bash scripts/llvm/clang-tidy.sh --target aarch64

# 应用自动修复
bash scripts/llvm/clang-tidy.sh --fix
```

检查项配置见仓库根 `.clang-tidy`; 仅分析自有代码 (`src/`, `test/`), 官方样例与 vcpkg 依赖自动过滤。

> **aarch64 模式说明**: GCC 交叉编译数据库含 `-fdeps-format=p1689r5` /
> `-fmodule-mapper=...` / `-fmodules-ts` 等 Clang 不识别的 GCC 15+ 参数,
> 脚本会自动净化到 `<build-dir>/clang-tidy-db/compile_commands.json` 后再分析,
> 无需手工处理 (x86 侧 Clang 数据库无此问题, 原库直接使用)。

---

## 4. Sanitizers 与覆盖率

**ASan/UBSan** 已内建于 `debug` 预设, 直接运行测试即可:

```bash
ctest --preset debug
```

> **Sanitizer 运行库**: 开发容器镜像已内置 LLVM 23 compiler-rt
> (`libclang_rt.asan*/ubsan*/profile*` 等位于 clang 的 resource-dir 下),
> Clang 直接使用自带静态 runtime, 无需 GCC 桥接或 `LD_PRELOAD`。
> 预设已启用 `-fno-sanitize-recover=all` (sanitizer 报错立即中止, 便于及早暴露问题)。
>
> 如需在无 compiler-rt 的环境中构建, 可回退为
> `-fno-sanitize-link-runtime` + `CMAKE_EXE_LINKER_FLAGS_DEBUG="-Wl,--no-as-needed -lasan -lubsan"`
> (复用 GCC 运行时, 注意保证 `libasan.so` 出现在 `DT_NEEDED` 首位)。

**TSan (线程检测)** 位于 `tsan` 预设 (与 ASan 互斥, 独立构建目录), 面向采集/命令/MQTT/WS 多线程流水线:

```bash
cmake --preset tsan
cmake --build --preset tsan
ctest --preset tsan
```

> **容器限制**: 本开发容器的 seccomp 策略禁止 `personality(ADDR_NO_RANDOMIZE)`:
> TSan 无法关闭 ASLR (报 `FATAL: ... unable to disable ASLR`), lldb 启动同样受限 (见 §8)。
> 构建不受影响。根治需改**容器启动参数** (Dockerfile 层无法设置, 因 seccomp 属运行期配置;
> 且改动后需重建容器才生效):
>
> - `docker run` 启动脚本: 追加 `--security-opt seccomp=unconfined`
> - docker compose: 服务下加 `security_opt: ["seccomp=unconfined"]`
> - VS Code 创建容器时 (`.devcontainer/devcontainer.json`): `"runArgs": ["--security-opt", "seccomp=unconfined"]`
>
> 最小权限做法 (仅放行必要调用, 其余过滤保持): 复制 Docker 默认 seccomp profile 后, 给
> `personality` 的允许参数追加 `262144` (ADDR_NO_RANDOMIZE), 再以
> `--security-opt seccomp=<custom.json>` 引用。
> 无 seccomp 限制的普通 Linux 主机/目标板无此问题。

**覆盖率** (LLVM 源码级):

```bash
bash scripts/llvm/clang-coverage.sh          # 构建 + 测试 + 文本报告 (report.txt / detail.txt)
bash scripts/llvm/clang-coverage.sh --html   # 额外生成 HTML 报告
```

产物位于 `build/x86_64-linux/cov/coverage/`; 报告自动排除第三方与样例代码。

---

## 5. llvm-mca 微架构性能建模

对 aarch64 交付产物中的关键函数做静态流水线建模 (RK3588S: 大核 `cortex-a76`, 小核 `cortex-a55`):

```bash
# 从目标文件/可执行文件提取函数并分析
bash scripts/llvm/llvm-mca.sh \
    --object build/aarch64-linux/release/samples/sample_c++/platform/linux/cy_psdk/CMakeFiles/cy_psdk.dir/src/manager/catalog/client/CatalogRuntime.cpp.o \
    --function <mangled-symbol> \
    --cpu cortex-a76

# 或直接分析一个汇编文件
bash scripts/llvm/llvm-mca.sh --asm /tmp/kernel.s --cpu cortex-a55 --timeline
```

符号名可用 `llvm-objdump -t <object> | llvm-cxxfilt | grep <函数名>` 查找
(demangle 名与符号对照), 或 `llvm-cxxfilt <符号>` 直接反解。分析结果给出吞吐、
延迟、端口压力, 用于定位热点与验证优化效果。

> **关于反汇编输入**: 目标文件反汇编产物中, 分支/调用目标只有地址而无标签,
> 脚本会将这些不完整指令行 (裸 `b`/`bl`/`cbz` 等) 剔除 —— 它们不影响计算段
> 吞吐建模。若需要完整精确建模 (含控制流), 请用 `--asm` 传入 `gcc -S` /
> `clang -S` 生成的带标签汇编。

---

## 6. aarch64 交叉编译与静态运行库

`aarch64` 预设使用 `aarch64-linux-gnu-g++` (GCC 16) 并附加:

```
CMAKE_EXE_LINKER_FLAGS = -static-libstdc++ -static-libgcc
```

目的: 避免目标板 libstdc++/libgcc 版本低于编译工具链时无法启动。
配套的 `libs/` 自包含运行库与 `run.sh` 机制保持不变。

**验证方式** (已实测): 产物的动态依赖应只剩 `libc.so.6`:

```bash
aarch64-linux-gnu-readelf -d build/aarch64-linux/release/bin/cy_psdk | grep NEEDED
# 预期: 仅 libc.so.6 (无 libstdc++.so.6 / libgcc_s.so.1)
```

**跨架构冒烟测试**: 容器已预装 `qemu-aarch64-static`, 可在 x86 上直接运行
aarch64 产物做基本验证 (不必上板):

```bash
qemu-aarch64-static -L /usr/aarch64-linux-gnu build/aarch64-linux/release/bin/cy_psdk --help
```

---

## 7. PGO (可选进阶)

GCC 的 PGO 需要**在目标板上**采集真实运行剖面:

```bash
# (1) 在板上生成插桩版本: 追加 -fprofile-generate
#     建议独立构建目录: build/aarch64-linux/pgo-gen
#     cmake -DCMAKE_CXX_FLAGS="-fprofile-generate=/root/pgo-data" ...
# (2) 板上运行典型作业, 生成 .gcda
# (3) 回传 .gcda, 用 -fprofile-use 重新编译
#     cmake -DCMAKE_CXX_FLAGS="-fprofile-use=/root/pgo-data -fprofile-correction" ...
```

注意事项:
- 采集与使用必须使用**同一 GCC 版本与同一路径**;
- 建议开启 `-fprofile-correction` 容忍多线程采集的计数器不精确;
- 如需 AutoFDO (基于 perf 采样) 或 BOLT: 仅适用于 LLVM 构建链。容器已提供
  `llvm-profgen` / `llvm-bolt` 全套工具, 但本交付链为 GCC 且 x86 产物不交付, 故未接入。

---

## 8. LLDB 原生调试 (lldb-dap)

VS Code 直接用 LLVM 23 自带调试栈调试 `debug` 预设产物 (ASan/UBSan 插桩):

```bash
cmake --preset debug && cmake --build --preset debug
# VS Code: F5 -> "LLDB: cy_psdk (debug 预设)"
```

配置见 `psdk/.vscode/launch.json`; 扩展为官方 `llvm-vs-code-extensions.lldb-dap`
(使用 PATH 中的 `lldb-dap` 23.1.2)。两处**容器适配** (均已实测):

- **seccomp**: 容器禁止 `personality(ADDR_NO_RANDOMIZE)`, 而 lldb 默认
  "先关 ASLR 再启动", 会报 `personality set failed`; launch.json 已用
  `initCommands: settings set target.disable-aslr false` 取消该行为。
  根治方案 (容器运行参数): `--security-opt seccomp=unconfined` —— 同时修复
  TSan 无法运行的问题 (见 §4)。
- **LSan**: 被调试进程已处于 lldb 跟踪下, LeakSanitizer 无法再次 ptrace,
  会以退出码 1 中止; 调试会话已设 `ASAN_OPTIONS=detect_leaks=0`
  (常规运行与 ctest 不受影响, 泄漏检测仍开启)。

命令行冒烟 (无需 VS Code, 已实测可停在 main 断点):

```bash
lldb --batch -o "settings set target.disable-aslr false" \
     -o "target create build/x86_64-linux/debug/bin/cy_psdk" \
     -o "breakpoint set --name main" -o run -o "frame info" -o quit
```

---

## 9. 模糊测试 (libFuzzer)

`fuzz` 预设构建两个覆盖率引导模糊器 (libFuzzer + ASan/UBSan, 仅 x86 Clang):

| 目标 | 覆盖路径 |
| --- | --- |
| `fuzz_probe_packet` | `decodeProbePacket` / `decodeAnnouncement` (SWMP UDP 报文) + 解码→重编码一致性回环 |
| `fuzz_json_codec` | `JsonCodec::registrationToJson` / `statusToJson` + 发送路径 `dump()` + `json::parse` 解析语义 |

```bash
cmake --preset fuzz
cmake --build --preset fuzz

# 种子语料在 fuzz/corpus/<目标>/; 变异结果建议写入独立工作目录 (避免污染种子语料):
mkdir -p /tmp/fuzz-work && cp -r fuzz/corpus/* /tmp/fuzz-work/
build/x86_64-linux/fuzz/bin/fuzz_probe_packet /tmp/fuzz-work/probe_packet -runs=100000
build/x86_64-linux/fuzz/bin/fuzz_json_codec  /tmp/fuzz-work/json_codec  -runs=100000
```

> 崩溃样本以 `crash-*` 落盘当前目录 (可用 `-artifact_prefix=<dir>/` 指定位置);
> ASan/UBSan 命中的内存/UB 问题会直接中止并输出栈回溯 (llvm-symbolizer 自动符号化)。

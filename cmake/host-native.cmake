# 宿主原生工具链 (目标架构 == 宿主架构)
#
# 供「检错 / 分析 / 调试」类预设使用 (debug / asan / tsan / cov / fuzz / perf)
# 目标架构跟随宿主: x86_64 宿主编译 x86_64, aarch64 宿主编译 aarch64,
# 于是同一套预设在任何开发容器里都能用 —— 这些预设与平台无关
#
# 与 cmake/x86_64-linux-gnu.cmake、cmake/aarch64-linux-gnu.cmake 的区别:
#   那两个文件固定目标架构 (因此是「交付」用的: x86_64 交付 / aarch64 交付),
#   本文件让目标架构跟随宿主 (因此是「开发」用的)
#
# vcpkg 依赖树与 triplet 同样按宿主选择, 并复用既有的两棵依赖树, 不新增下载:
#   x86_64 宿主 -> vcpkg_installed-clang / x64-linux
#   aarch64 宿主 -> vcpkg_installed/arm64 / arm64-linux
#
# 作为 vcpkg 的 VCPKG_CHAINLOAD_TOOLCHAIN_FILE 使用 (见 CMakePresets.json 的 host-clang 预设)
# 注意: vcpkg.cmake 会先 include 本文件 (第 209 行), 之后才读取 VCPKG_INSTALLED_DIR
# (第 420 行), 所以在这里设置 _VCPKG_INSTALLED_DIR 是生效的

set (CMAKE_SYSTEM_NAME Linux)

# 宿主架构探测 + 自装工具链根目录 + 探针宏（三个项目统一，见该文件注释）
include (${CMAKE_CURRENT_LIST_DIR}/toolchain-common.cmake)

if(SWARM_HOST_ARM)
    set (CMAKE_SYSTEM_PROCESSOR aarch64)
    set (_host_native_vcpkg_dir "${CMAKE_CURRENT_LIST_DIR}/../vcpkg_installed/arm64")
    set (_host_native_vcpkg_triplet "arm64-linux")
else()
    set (CMAKE_SYSTEM_PROCESSOR x86_64)
    set (_host_native_vcpkg_dir "${CMAKE_CURRENT_LIST_DIR}/../vcpkg_installed-clang")
    set (_host_native_vcpkg_triplet "x64-linux")
endif()

# 编译器由预设给出（host-clang 预设设 clang++），本文件不覆盖它，只在末尾用探针校验真的能用
get_filename_component (_host_native_vcpkg_dir "${_host_native_vcpkg_dir}" ABSOLUTE)
set (
    _VCPKG_INSTALLED_DIR
    "${_host_native_vcpkg_dir}"
    CACHE PATH "vcpkg 依赖树 (按宿主选择)" FORCE
)
set (
    VCPKG_TARGET_TRIPLET
    "${_host_native_vcpkg_triplet}"
    CACHE STRING "vcpkg triplet (按宿主选择)" FORCE
)

message (
    STATUS
        "工具链 [host-native]: 宿主 ${CMAKE_HOST_SYSTEM_PROCESSOR} -> 目标 ${CMAKE_SYSTEM_PROCESSOR}, vcpkg ${_host_native_vcpkg_triplet}"
)

swarm_verify_toolchain ()

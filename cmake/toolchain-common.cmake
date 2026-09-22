if(NOT DEFINED CMAKE_HOST_SYSTEM_PROCESSOR OR CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "")
    execute_process (
        COMMAND uname -m
        OUTPUT_VARIABLE CMAKE_HOST_SYSTEM_PROCESSOR
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
endif()
if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
    set (SWARM_HOST_ARM TRUE)
else()
    set (SWARM_HOST_ARM FALSE)
endif()

set (
    SWARM_GCC_ROOT
    "/opt/gcc-16"
    CACHE PATH "自装 GCC 工具链根目录（含 bin/）"
)
if(NOT EXISTS "${SWARM_GCC_ROOT}/bin/g++")
    file (GLOB _swarm_gcc_alts "/opt/gcc-*")
    list (SORT _swarm_gcc_alts COMPARE NATURAL)
    list (LENGTH _swarm_gcc_alts _swarm_gcc_n)
    if(_swarm_gcc_n GREATER 0)
        list (
            GET
            _swarm_gcc_alts
            -1
            _swarm_gcc_alt
        )
        set (
            SWARM_GCC_ROOT
            "${_swarm_gcc_alt}"
            CACHE PATH "自装 GCC 工具链根目录（含 bin/）" FORCE
        )
        message (STATUS "自动改用自装工具链: ${SWARM_GCC_ROOT}")
    endif()
endif()
if(NOT EXISTS "${SWARM_GCC_ROOT}/bin/g++")
    message (FATAL_ERROR "找不到自装 GCC 工具链: ${SWARM_GCC_ROOT}/bin/g++\n" "本镜像不使用系统自带 GCC，请用 -DSWARM_GCC_ROOT=<工具链根目录> 指定")
endif()

# 工具链可用性探针: 系统自带 GCC 可能残缺到找不到自己的 stddef.h；在这里就说清楚，
# 不要让错误拖到构建期以 "stddef.h: 没有那个文件或目录" 这种误导人的形式出现
macro(swarm_verify_toolchain)
    if(DEFINED CMAKE_CXX_COMPILER AND CMAKE_CXX_COMPILER)
        execute_process (
            COMMAND ${CMAKE_CXX_COMPILER} -x c++ -include stddef.h -E /dev/null
            RESULT_VARIABLE _swarm_probe_rc
            OUTPUT_QUIET
            ERROR_VARIABLE _swarm_probe_err
        )
        if(NOT
           _swarm_probe_rc
           STREQUAL
           "0"
        )
            message (
                FATAL_ERROR
                    "工具链不可用: ${CMAKE_CXX_COMPILER}\n"
                    "预处理 #include <stddef.h> 失败: ${_swarm_probe_err}\n"
                    "本镜像不使用系统自带 GCC，请用 -DSWARM_GCC_ROOT=<工具链根目录> 指定自装工具链\n"
                    "若构建目录里已有旧缓存（CMAKE_CXX_COMPILER 被缓存住），请删除该构建目录后重新配置"
            )
        endif()
    endif()
endmacro()

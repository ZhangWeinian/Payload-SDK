cmake_minimum_required(VERSION 3.5)
project(dji_sdk_demo_on_rpi_cxx C CXX)

set(CMAKE_C_FLAGS "-pthread")
set(CMAKE_CXX_FLAGS "-pthread")
set(CMAKE_EXE_LINKER_FLAGS "-pthread")
add_definitions(-D_GNU_SOURCE)

# 官方 C++ 版缺失的平台宏 (C 版与 manifold3/nvidia_jetson C++ 版均有定义)。
# 缺失时 flight_controller 模块内部的条件编译段被跳过, 导致编译失败且功能缺失。
add_definitions(-DSYSTEM_ARCH_LINUX=1)

if (MEMORY_LEAK_CHECK_ON MATCHES TRUE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=leak")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=leak")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lasan")
endif ()

if (BUILD_TEST_CASES_ON MATCHES TRUE)
    set(COMMON_CXX_FLAGS "-std=c++11 -pthread")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fprofile-arcs -ftest-coverage -Wno-deprecated-declarations")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fprofile-arcs -ftest-coverage")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fprofile-arcs -ftest-coverage -lgcov")
endif ()

include_directories(../../../module_sample)
include_directories(../../../../sample_c/module_sample)
include_directories(../common)
include_directories(application)

file(GLOB_RECURSE MODULE_SAMPLE_SRC
        ../../../module_sample/liveview/*.c*
        ../../../module_sample/camera_manager/*.c*
        ../../../module_sample/perception/*.c*
        ../../../module_sample/gimbal/*.c*
        ../../../module_sample/flight_controller/*.c*
        ../../../module_sample/hms_manager/*.c*
        ../../../../sample_c/module_sample/*.c
        ../../../module_sample/widget_manager/*.c*
        )
file(GLOB_RECURSE MODULE_COMMON_SRC ../common/*.c*)
file(GLOB_RECURSE MODULE_HAL_SRC hal/*.c*)
file(GLOB_RECURSE MODULE_APP_SRC application/*.c*)

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../common/3rdparty)

link_directories(${CMAKE_CURRENT_LIST_DIR}/../../../../../psdk_lib/lib/aarch64-linux-gnu-gcc)
link_libraries(${CMAKE_CURRENT_LIST_DIR}/../../../../../psdk_lib/lib/aarch64-linux-gnu-gcc/libpayloadsdk.a -lstdc++)

add_executable(${PROJECT_NAME}
        ${MODULE_APP_SRC}
        ${MODULE_SAMPLE_SRC}
        ${MODULE_COMMON_SRC}
        ${MODULE_HAL_SRC})

include_directories(${CMAKE_CURRENT_LIST_DIR}/../../../../../psdk_lib/include)

if (BUILD_CROSS_COMPILE MATCHES TRUE)
    # Try to see if user has OpenCV installed
    # if yes, default callback will display the image
    find_package(OpenCV QUIET)
    if (OpenCV_FOUND)
        message("\n${PROJECT_NAME}...")
        message(STATUS "Found OpenCV installed in the system, will use it to display image in AdvancedSensing APIs")
        message(STATUS " - Includes: ${OpenCV_INCLUDE_DIRS}")
        message(STATUS " - Libraries: ${OpenCV_LIBRARIES}")
        add_definitions(-DOPEN_CV_INSTALLED)
    else ()
        message(STATUS "Did not find OpenCV in the system, image data is inside RecvContainer as raw data")
    endif ()

    find_package(FFMPEG REQUIRED)
    if (FFMPEG_FOUND)
        message(STATUS "Found FFMPEG installed in the system")
        message(STATUS " - Includes: ${FFMPEG_INCLUDE_DIR}")
        message(STATUS " - Libraries: ${FFMPEG_LIBRARIES}")

        EXECUTE_PROCESS(COMMAND ffmpeg -version
                OUTPUT_VARIABLE ffmpeg_version_output
                OUTPUT_STRIP_TRAILING_WHITESPACE
                )
        string(REGEX MATCH "version.*Copyright" ffmpeg_version_line ${ffmpeg_version_output})
        string(REGEX MATCH " .* " ffmpeg_version ${ffmpeg_version_line})
        string(REGEX MATCH "^ 5.*$" ffmpeg_major_version ${ffmpeg_version})

        if (HEAD${ffmpeg_major_version} STREQUAL "HEAD")
            message(STATUS " - Version: ${ffmpeg_version}")
        else ()
            message(FATAL_ERROR " - Not support FFMPEG version: ${ffmpeg_major_version}, please install 4.x.x instead.")
        endif ()

        target_link_libraries(${PROJECT_NAME} ${FFMPEG_LIBRARIES})
        include_directories(${FFMPEG_INCLUDE_DIR})
        add_definitions(-DFFMPEG_INSTALLED)
    else ()
        message(STATUS "Cannot Find FFMPEG")
    endif (FFMPEG_FOUND)

    find_package(OPUS REQUIRED)
    if (OPUS_FOUND)
        message(STATUS "Found OPUS installed in the system")
        message(STATUS " - Includes: ${OPUS_INCLUDE_DIR}")
        message(STATUS " - Libraries: ${OPUS_LIBRARY}")

        add_definitions(-DOPUS_INSTALLED)
        target_link_libraries(${PROJECT_NAME} /usr/local/lib/libopus.a)
    else ()
        message(STATUS "Cannot Find OPUS")
    endif (OPUS_FOUND)

    find_package(LIBUSB REQUIRED)
    if (LIBUSB_FOUND)
        message(STATUS "Found LIBUSB installed in the system")
        message(STATUS " - Includes: ${LIBUSB_INCLUDE_DIR}")
        message(STATUS " - Libraries: ${LIBUSB_LIBRARY}")

        add_definitions(-DLIBUSB_INSTALLED)
        target_link_libraries(${PROJECT_NAME} usb-1.0)
    else ()
        message(STATUS "Cannot Find LIBUSB")
    endif (LIBUSB_FOUND)
endif()

if (NOT EXECUTABLE_OUTPUT_PATH)
    set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/bin)
endif ()

target_link_libraries(${PROJECT_NAME} m dl)

target_include_directories(${PROJECT_NAME} PRIVATE ${OpenCV_INCLUDE_DIRS})
if (OpenCV_FOUND)
    target_link_libraries(${PROJECT_NAME} ${OpenCV_LIBS})
endif ()

# ---- 自包含交付目录 (与主工程 cy_psdk 同一套机制) -------------------------------
# 目标板的系统 glibc 往往远旧于交叉工具链自带的那一套: 实测在 Ubuntu 24.04 上直接运行
# 主程序会报 "version `GLIBC_2.43' not found / GLIBC_2.42 not found"。
# 因此把交叉工具链的整套运行库随交付目录打包, 由 run.sh 用打包的 ld-linux-aarch64.so.1
# + --library-path libs/ 启动, 完全不依赖板端系统库 (与 cy_psdk 交付目录完全一致)。
#
# 交付目录结构 (与 cy_tools/rsync.sh 的上传路径约定一致):
#   build/aarch64-linux/release/bin/ = dji_sdk_demo_on_rpi_cxx + run.sh + libs/
get_filename_component (PSDK_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
set (RUN_SCRIPT_SOURCE_FILE "${PSDK_REPO_ROOT}/bash/run.sh")
set (SYSTEM_LIBS_SOURCE_DIR "${PSDK_REPO_ROOT}/system_lib/lib/aarch64-linux")
set (RUN_SCRIPT_DEST_FILE "${EXECUTABLE_OUTPUT_PATH}/run.sh")
set (LIBS_DEST_DIR "${EXECUTABLE_OUTPUT_PATH}/libs")

if (EXISTS ${SYSTEM_LIBS_SOURCE_DIR})
    # 先清空 libs/ 再拷贝, 避免残留其它架构/旧版本的运行库;
    # 动态链接器必须显式保留可执行位, 否则 run.sh 会回退板端系统解释器而启动失败。
    add_custom_target (
        package_self_contained ALL
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${LIBS_DEST_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${LIBS_DEST_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different "${SYSTEM_LIBS_SOURCE_DIR}" "${LIBS_DEST_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${RUN_SCRIPT_SOURCE_FILE}" "${RUN_SCRIPT_DEST_FILE}"
        COMMAND chmod +x "${RUN_SCRIPT_DEST_FILE}"
        COMMAND chmod +x "${LIBS_DEST_DIR}/ld-linux-aarch64.so.1"
        COMMENT "正在组装自包含交付目录 (run.sh + libs/)"
        VERBATIM
    )
    add_dependencies (package_self_contained ${PROJECT_NAME})
else ()
    message (WARNING "未找到 ${SYSTEM_LIBS_SOURCE_DIR}, 不打包运行库 (交付目录将依赖板端系统库)")
endif ()

# ---- 样例运行时资源 -----------------------------------------------------------
# 多个官方样例用 DjiUserUtil_GetCurrentFileDirPath(__FILE__) 推出资源目录
# (media_file/、widget_file/、hms_text/ 等) —— 它取的是 __FILE__ 的目录。
# 交叉编译时 __FILE__ 是编译机上的绝对路径 (/home/vscode/workspace/...), 板端不存在,
# 于是所有涉及文件读写的样例全部失败 (实测: camera_emu 刷屏 "open video file fail."、
# widget 初始化报 "Input dir can't be NULL")。
# 把仓库根映射为 "." ⇒ __FILE__ 变成相对路径, 按【当前工作目录】解析;
# run.sh 启动时会先 cd 到交付目录, 而资源树也随包发布到同一目录下, 于是恰好命中。
target_compile_options (${PROJECT_NAME} PRIVATE "-ffile-prefix-map=${PSDK_REPO_ROOT}=.")

# 样例运行时要读写的资源目录 (保持源码树的相对结构; 均为代码中 "%s..." 实际拼到的目录)。
# 不含 sample_c++/module_sample/liveview/data: 那是 103MB 的 YOLO/TensorFlow 模型,
#   且取流/相机管理属高级许可功能, 基础许可下本就用不到。
# 也不含 *_c 目录: 它们是被 #include 的编译期二进制数组头文件, 不参与运行时读取。
set (SAMPLE_RESOURCE_DIRS
    "sample_c/module_sample/camera_emu/media_file"
    "sample_c/module_sample/hms/data"
    "sample_c/module_sample/hms/hms_text"
    "sample_c/module_sample/mop_channel/mop_channel_test_file"
    "sample_c/module_sample/waypoint_v3/waypoint_file"
    "sample_c/module_sample/widget/widget_file"
    "sample_c/module_sample/widget_interaction_test/widget_file"
    "sample_c++/module_sample/flight_controller/config"
    "sample_c++/module_sample/widget_manager/data"
)

set (SAMPLE_RESOURCE_COPY_CMDS "")
foreach (REL_DIR IN LISTS SAMPLE_RESOURCE_DIRS)
    list (
        APPEND SAMPLE_RESOURCE_COPY_CMDS
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
                "${PSDK_REPO_ROOT}/samples/${REL_DIR}"
                "${EXECUTABLE_OUTPUT_PATH}/samples/${REL_DIR}"
    )
endforeach ()

add_custom_target (
    package_sample_resources ALL
    ${SAMPLE_RESOURCE_COPY_CMDS}
    COMMENT "正在发布样例运行时资源 (media_file/ widget_file/ hms_text/ ...)"
    VERBATIM
)
add_dependencies (package_sample_resources ${PROJECT_NAME})

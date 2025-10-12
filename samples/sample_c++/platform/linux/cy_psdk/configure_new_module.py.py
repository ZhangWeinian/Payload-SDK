#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os
import sys
import textwrap

# ==============================================================================
# --- 配置区域: 如果未来需求变化，只需修改此处 ---
# ==============================================================================

# 1. 待删除的文件路径
FILE_TO_DELETE = os.path.join("application", "main.cpp")

# 2. 开发者信息 (application/dji_sdk_app_info.h)
APP_INFO_PATH = os.path.join("application", "dji_sdk_app_info.h")
APP_INFO_BEFORE = textwrap.dedent(
    """\
    #define USER_APP_NAME               "your_app_name"
    #define USER_APP_ID                 "your_app_id"
    #define USER_APP_KEY                "your_app_key"
    #define USER_APP_LICENSE            "your_app_license"
    #define USER_DEVELOPER_ACCOUNT      "your_developer_account"
    #define USER_BAUD_RATE              "460800"
"""
)
APP_INFO_AFTER = textwrap.dedent(
    """\
    #define USER_APP_NAME               "test"
    #define USER_APP_ID                 "168809"
    #define USER_APP_KEY                "fa42c26a9253e8d492b12e0baf83fdb"
    #define USER_APP_LICENSE                                                                                                         \\
     "yEduca4jloV78YzpEXaJ2W9Ys6VWMpRkTX3nOO4OyWdZBnDV59xyWm5kkvg+"                                                                  \\
     "zyBibGjIc2PuAoRxAzOXIMWXahiG0cfR17naHK1MeQgmsJKDQPi7tbOehUges3m4ib71tZ3sO4TFEZGRsz8MJWrPRTqsnNgzqnkhh7lAq+"                    \\
     "pJVn8u1UbH5GOCj2qrzzR9pW2UJiboIcZPSA5N0ygDHg+A4MdFWuRBHxIOIKPOR0MdHf1x2P1VVoOiJCvwcOm98ztlOzaCrWXPdutyPJynGyHQrBNA3ZdpdnmCt7+" \\
     "hCo17lhXxUa+3jFxWDSA9aaf3KM9ZlBdC0daaGzT3QAs3Rkj42w=="
    #define USER_DEVELOPER_ACCOUNT      "accepted"
    #define USER_BAUD_RATE              "460800"
"""
)

# 3. 接口定义 (hal/hal_uart.h)
UART_INFO_PATH = os.path.join("hal", "hal_uart.h")
UART_INFO_BEFORE = textwrap.dedent(
    """\
    #define LINUX_UART_DEV1    "/dev/ttyUSB0"
    #define LINUX_UART_DEV2    "/dev/ttyACM0"
"""
)
UART_INFO_AFTER = textwrap.dedent(
    """\
    #define LINUX_UART_DEV1    "/dev/ttyUSB0"
    #define LINUX_UART_DEV2    ""
"""
)

# 4. 日志重定向 (application/application.cpp)
APP_CPP_PATH = os.path.join("application", "application.cpp")
LOG_BLOCK_1_BEFORE = textwrap.dedent(
    """\
    returnCode = DjiLogger_AddConsole(&printConsole);
    if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
     throw std::runtime_error("Add printf console error.");
    }"""
)
LOG_BLOCK_1_AFTER = textwrap.dedent(
    """\
    // returnCode = DjiLogger_AddConsole(&printConsole);
    // if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    //  throw std::runtime_error("Add printf console error.");
    // }"""
)

LOG_BLOCK_2_BEFORE = textwrap.dedent(
    """\
    returnCode = DjiLogger_AddConsole(&localRecordConsole);
    if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
     throw std::runtime_error("Add printf console error.");
    }"""
)
LOG_BLOCK_2_AFTER = textwrap.dedent(
    """\
    // returnCode = DjiLogger_AddConsole(&localRecordConsole);
    // if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    //  throw std::runtime_error("Add printf console error.");
    // }"""
)


# ==============================================================================
# --- 脚本核心逻辑: 一般无需修改 ---
# ==============================================================================


def check_environment():
    """检查脚本运行环境是否正确"""
    print("1. 检查运行环境...")
    if not (os.path.isdir("application") and os.path.isdir("hal")):
        print("\033[91m[错误]\033[0m 未找到 'application' 和 'hal' 文件夹。")
        print("      请确保您在 'cy_psdk' 项目的根目录下运行此脚本。")
        sys.exit(1)
    print("\033[92m[成功]\033[0m 环境检查通过。\n")


def delete_file(filepath):
    """(对应步骤3) 删除指定文件"""
    print(f"2. 处理文件删除: {filepath}...")
    if os.path.exists(filepath):
        try:
            os.remove(filepath)
            print(f"\033[92m[成功]\033[0m 已删除文件 '{filepath}'。")
        except OSError as e:
            print(f"\033[91m[错误]\033[0m 删除文件 '{filepath}' 失败: {e}")
            return False
    else:
        print(f"\033[93m[跳过]\033[0m 文件 '{filepath}' 不存在，无需删除。")
    print("")
    return True


def apply_modification(filepath, old_content, new_content):
    """
    通用的文件内容查找与替换函数。
    它会检查文件内容，只有在找到完全匹配的旧内容时才执行替换。
    """
    if not os.path.exists(filepath):
        print(f"\033[91m[错误]\033[0m 目标文件不存在: {filepath}")
        return False

    try:
        with open(filepath, "r", encoding="utf-8") as f:
            content = f.read()
    except Exception as e:
        print(f"\033[91m[错误]\033[0m 读取文件失败: {filepath}, 原因: {e}")
        return False

    # 移除字符串两端的空白符，以实现更可靠的匹配
    old_content_stripped = old_content.strip()
    new_content_stripped = new_content.strip()

    if old_content_stripped in content:
        # 执行替换
        updated_content = content.replace(old_content_stripped, new_content_stripped)
        try:
            with open(filepath, "w", encoding="utf-8") as f:
                f.write(updated_content)
            print(f"\033[92m[成功]\033[0m 已更新文件: {filepath}")
            return True
        except Exception as e:
            print(f"\033[91m[错误]\033[0m 写入文件失败: {filepath}, 原因: {e}")
            return False
    elif new_content_stripped in content:
        print(f"\033[93m[跳过]\033[0m 文件 '{filepath}' 已被配置，无需修改。")
        return True
    else:
        print(f"\033[91m[错误]\033[0m 在 '{filepath}' 中未找到预期的旧配置内容。")
        print("      请手动检查该文件是否为新模块的原始文件。")
        return False


def main():
    """脚本主入口"""
    print("--- CY PSDK 新模块自动化配置脚本 ---")

    check_environment()

    # 执行步骤3
    success_step3 = delete_file(FILE_TO_DELETE)

    # 执行步骤4
    print("3. (对应步骤4) 开始更新配置文件...")

    print("\n   -> 更新开发者信息...")
    success_app_info = apply_modification(
        APP_INFO_PATH, APP_INFO_BEFORE, APP_INFO_AFTER
    )

    print("\n   -> 更新接口定义...")
    success_uart = apply_modification(UART_INFO_PATH, UART_INFO_BEFORE, UART_INFO_AFTER)

    print("\n   -> 更新日志重定向 (区块1)...")
    success_log1 = apply_modification(
        APP_CPP_PATH, LOG_BLOCK_1_BEFORE, LOG_BLOCK_1_AFTER
    )

    print("\n   -> 更新日志重定向 (区块2)...")
    success_log2 = apply_modification(
        APP_CPP_PATH, LOG_BLOCK_2_BEFORE, LOG_BLOCK_2_AFTER
    )

    print("\n-----------------------------------------")
    all_tasks_successful = all(
        [success_step3, success_app_info, success_uart, success_log1, success_log2]
    )

    if all_tasks_successful:
        print("\033[92m[任务完成] 所有自动化步骤均已成功执行！\033[0m")
        print("现在您可以继续清理构建缓存并重新编译项目了。")
    else:
        print(
            "\033[91m[任务失败] 部分自动化步骤未能成功。请检查上面的错误信息。\033[0m"
        )
        sys.exit(1)


if __name__ == "__main__":
    main()

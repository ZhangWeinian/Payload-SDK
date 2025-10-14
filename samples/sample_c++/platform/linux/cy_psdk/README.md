# CY PSDK 开发组件信息

## 开发模块信息

目前基础开发模块：manifold2
后期目标基础模块：raspberry_pi

## 如何替换基础模块？

替换基础模块的流程分为三个阶段：准备工作 -> 模块配置 -> 编译运行

**阶段一：准备工作**

1. 在 cy_psdk 目录下，删除旧的 application 和 hal 文件夹
2. 拷贝目标模块（如 raspberry_pi ）的 application 和 hal 文件夹到 cy_psdk 目录中

**阶段二：模块配置**
完成准备工作后，请根据您的开发环境选择以下任意一种方案进行配置

### 方案A：使用自动化脚本（推荐）

推荐有 Python 环境的用户优先使用此方法，可以一键完成配置，高效且不易出错

3. 在项目根目录下打开终端，运行命令：

```bash
python3 configure_new_module.py
```

4. 脚本将自动完成文件删除和所有必要的代码修改。执行成功后，直接跳至【阶段三】。

### 方案B：手动修改配置

如果您的开发环境中没有 Python，或者希望手动控制每一个修改，请按照以下步骤操作

3. 删除拷贝后 application 文件夹中的 main.cpp 文件
4. 根据下文【开发者信息】、【接口定义】和【日志重定向】章节的说明，手动修改相应文件中的配置信息

## 开发者信息

文件：application/dji_sdk_app_info.h
配置：

**阶段三：编译运行**
完成上述任一方案的配置后，继续执行以下步骤：

5. 清理之前的构建缓存（如果有），然后重新编译项目
6. 根据编译器的错误提示，可能需要适当加强新的 application 和 hal 中的部分代码以符合当前编译要求（例如修复 -Werror=unused-result 警告）

## 开发者信息

文件：application/dji_sdk_app_info.h
配置：

```cpp
// 修改前：
#define USER_APP_NAME               "your_app_name"
#define USER_APP_ID                 "your_app_id"
#define USER_APP_KEY                "your_app_key"
#define USER_APP_LICENSE            "your_app_license"
#define USER_DEVELOPER_ACCOUNT      "your_developer_account"
#define USER_BAUD_RATE              "460800"

// 修改后：
#define USER_APP_NAME               "test"
#define USER_APP_ID                 "168809"
#define USER_APP_KEY                "fa42c26a9253e8d492b12e0baf83fdb"
#define USER_APP_LICENSE                                                                                                         \
 "yEduca4jloV78YzpEXaJ2W9Ys6VWMpRkTX3nOO4OyWdZBnDV59xyWm5kkvg+"                                                                  \
 "zyBibGjIc2PuAoRxAzOXIMWXahiG0cfR17naHK1MeQgmsJKDQPi7tbOehUges3m4ib71tZ3sO4TFEZGRsz8MJWrPRTqsnNgzqnkhh7lAq+"                    \
 "pJVn8u1UbH5GOCj2qrzzR9pW2UJiboIcZPSA5N0ygDHg+A4MdFWuRBHxIOIKPOR0MdHf1x2P1VVoOiJCvwcOm98ztlOzaCrWXPdutyPJynGyHQrBNA3ZdpdnmCt7+" \
 "hCo17lhXxUa+3jFxWDSA9aaf3KM9ZlBdC0daaGzT3QAs3Rkj42w=="
#define USER_DEVELOPER_ACCOUNT      "accepted"
#define USER_BAUD_RATE              "460800"

```

## 接口定义

文件：hal/hal_uart.h
配置：

```cpp
// 修改前：
#define LINUX_UART_DEV1    "/dev/ttyUSB0"
#define LINUX_UART_DEV2    "/dev/ttyACM0"

// 修改后：
#define LINUX_UART_DEV1    "/dev/ttyUSB0"
#define LINUX_UART_DEV2    ""
```

## 日志重定向

文件：application/application.cpp
配置：

```cpp
// 修改前：
if (DjiUser_LocalWriteFsInit(DJI_LOG_PATH) != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
 throw std::runtime_error("File system init error.");
}

returnCode = DjiLogger_AddConsole(&printConsole);
if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
 throw std::runtime_error("Add printf console error.");
}

returnCode = DjiLogger_AddConsole(&localRecordConsole);
if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
 throw std::runtime_error("Add printf console error.");
}

// 修改后：
// if (DjiUser_LocalWriteFsInit(DJI_LOG_PATH) != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
//  throw std::runtime_error("File system init error.");
// }

// returnCode = DjiLogger_AddConsole(&printConsole);
// if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
//  throw std::runtime_error("Add printf console error.");
// }

// returnCode = DjiLogger_AddConsole(&localRecordConsole);
// if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
//  throw std::runtime_error("Add printf console error.");
// }
```

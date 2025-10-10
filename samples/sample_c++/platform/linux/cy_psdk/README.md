# CY PSDK 开发组件信息

## 重要信息

目前基础开发模块：manifold2
后期目标基础模块：raspberry_pi

## 如何替换基础模块？

1. 删除 my_dji 文件夹中的 application 和 hal 文件夹
2. 拷贝目标模块的 application 和 hal 文件夹到 my_dji 中
3. 删除拷贝后 application 中的 main.cpp 文件
4. 按照如下的【开发者信息】和【接口定义】更新基础模块中的信息
5. 清理之前的构建缓存（如果有），重新编译项目
6. 可能要适当加强新的 application 和 hal 中的部分代码，以符合当前编译要求（-Werror=unused-result）。

## 开发者信息

文件：dji_sdk_app_info.h
配置：

```cpp
#define USER_APP_NAME "test"
#define USER_APP_ID   "168809"
#define USER_APP_KEY  "fa42c26a9253e8d492b12e0baf83fdb"
#define USER_APP_LICENSE                                                                                                         \
 "yEduca4jloV78YzpEXaJ2W9Ys6VWMpRkTX3nOO4OyWdZBnDV59xyWm5kkvg+"                                                                  \
 "zyBibGjIc2PuAoRxAzOXIMWXahiG0cfR17naHK1MeQgmsJKDQPi7tbOehUges3m4ib71tZ3sO4TFEZGRsz8MJWrPRTqsnNgzqnkhh7lAq+"                    \
 "pJVn8u1UbH5GOCj2qrzzR9pW2UJiboIcZPSA5N0ygDHg+A4MdFWuRBHxIOIKPOR0MdHf1x2P1VVoOiJCvwcOm98ztlOzaCrWXPdutyPJynGyHQrBNA3ZdpdnmCt7+" \
 "hCo17lhXxUa+3jFxWDSA9aaf3KM9ZlBdC0daaGzT3QAs3Rkj42w=="
#define USER_DEVELOPER_ACCOUNT "accepted"
#define USER_BAUD_RATE         "460800"

```

## 接口定义

文件：hal/hal_uart.h
配置：

```cpp
#define LINUX_UART_DEV1 "/dev/ttyUSB0"
#define LINUX_UART_DEV2 ""
```

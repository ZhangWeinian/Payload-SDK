# CY PSDK 开发组件信息

## 重要信息

目前基础开发模块：manifold2
后期目标基础模块：raspberry_pi

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

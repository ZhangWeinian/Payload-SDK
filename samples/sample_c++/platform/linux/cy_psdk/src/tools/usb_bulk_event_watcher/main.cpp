/**
 ********************************************************************
 * @file    main.cpp
 * @brief   usb_bulk_event_watcher —— 监听 FunctionFS 各实例 ep0 的事件流, 判断
 *          "飞机(USB Host)是否已配置我们", 结果落入状态文件。
 *
 * 为什么需要它:
 *   PSDK 的 USB Bulk 链路只有在飞机完成 SET_CONFIGURATION 之后才能承载数据。在飞机未配置
 *   该链路时注册 Bulk HAL, PSDK 会在 payload negotiate 阶段等超时 (225 TIMEOUT), 使整个
 *   Core init 失败。因此注册前必须先拿到一个可信的"主机已配置"信号。
 *
 * 为什么信号取 ep0 事件流, 而不是 /sys/class/udc/<udc>/state:
 *   dwc2 在 dr_mode=peripheral 下几乎不更新 state 字段 —— 2026-09-15 板测: 即使主机已
 *   发出 USBRst + EnumDone 完成高速握手, state 仍停在 "not attached"。主机发
 *   SET_CONFIGURATION 时内核会把 FUNCTIONFS_ENABLE 事件写进该实例 ep0 的读队列
 *   (drivers/usb/gadget/function/f_fs.c), 这是唯一第一手信号。
 *
 * 为什么必须用 C 而不是 shell:
 *   事件是 struct usb_functionfs_event 的 12 字节二进制 (union u 占 8 字节, type 在偏移 8),
 *   内核还要求读缓冲区不小于整个结构体 (f_fs.c: len < sizeof(event) ⇒ EINVAL)。bash 字符串
 *   无法保存 NUL, read/printf 也无法按定长解析二进制 (实测: bash `read -n 1` 的真实语义是
 *   "返回下一个非 NUL 字节"), 因此纯 shell 无法可靠解析该事件流。
 *
 * 事件类型 (内核 usb/functionfs-event.rst):
 *   BIND=1 UNBIND=2 ENABLE=3 DISABLE=4 SETUP=5 SUSPEND=6 RESUME=7
 *   只有 ENABLE 置位"已配置", DISABLE/UNBIND 清位, 其余事件不影响判定。
 *
 * 用法:
 *   usb_bulk_event_watcher <状态文件> <ep0 路径>...
 *   例: usb_bulk_event_watcher /run/usb_bulk_holder.state \
 *           /dev/usb-ffs/bulk1/ep0 /dev/usb-ffs/bulk2/ep0 /dev/usb-ffs/bulk3/ep0
 *   参数为普通文件时读到 EOF 即正常退出 (便于用合成事件流做自测)。
 *
 * 状态文件格式 (行尾 '\n'):
 *   host_configured=<0|1>
 *   <实例目录名>=<enabled|disabled>      # 如 bulk1=enabled, bulk2=disabled
 *
 * @copyright (c) 2026 CY. All rights reserved.
 *********************************************************************
 */

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

namespace
{
    // struct usb_functionfs_event (linux/usb/functionfs.h):
    //   union u { struct usb_ctrlrequest setup; } 8 字节 + type 1 字节 + pad 3 字节 = 12 字节
    // 注意: type 在偏移 8 而不是首字节; 读缓冲区必须 >= 12, 否则内核返回 EINVAL
    constexpr ::std::size_t kEventSize { 12 };
    constexpr ::std::size_t kEventTypeOffset { 8 };
    constexpr int           kPollTimeoutMs { 500 };

    // 事件类型 (内核 usb/functionfs-event.rst)
    constexpr ::std::uint32_t kEventUnbind { 2 };
    constexpr ::std::uint32_t kEventEnable { 3 };
    constexpr ::std::uint32_t kEventDisable { 4 };

    constexpr const char*     kTag { "[usb-bulk-watch]" };

    struct Instance
    {
        ::std::string path {};           // ep0 路径
        ::std::string label {};          // 状态文件里的短名 (ep0 所在目录名, 如 bulk1)
        int           fd { -1 };
        bool          enabled { false }; // 主机是否已配置该接口
        bool          closed { false };  // 已停止监听 (EOF 或错误)
    };

    // "/dev/usb-ffs/bulk1/ep0" -> "bulk1"
    ::std::string labelOf(const ::std::string& path)
    {
        const auto slash { path.find_last_of('/') };
        const auto directory { (slash == ::std::string::npos) ? path : path.substr(0, slash) };
        const auto lastSlash { directory.find_last_of('/') };
        return (lastSlash == ::std::string::npos) ? directory : directory.substr(lastSlash + 1);
    }

    // 写状态文件 (先写临时文件再 rename: 读取方不会看到写了一半的内容)
    bool writeStateFile(const char* path, const ::std::vector<Instance>& instances, bool hostConfigured)
    {
        const ::std::string temporary { ::std::string { path } + ".tmp" };
        ::FILE*             file { ::fopen(temporary.c_str(), "w") };
        if (file == nullptr)
        {
            ::fprintf(stderr, "%s 无法写入状态文件 %s: %s\n", kTag, temporary.c_str(), ::strerror(errno));
            return false;
        }

        ::fprintf(file, "host_configured=%d\n", hostConfigured ? 1 : 0);
        for (const auto& instance : instances)
        {
            ::fprintf(file, "%s=%s\n", instance.label.c_str(), instance.enabled ? "enabled" : "disabled");
        }

        if (::fclose(file) != 0 || ::rename(temporary.c_str(), path) != 0)
        {
            ::fprintf(stderr, "%s 状态文件落盘失败 (%s): %s\n", kTag, path, ::strerror(errno));
            return false;
        }
        return true;
    }

    // 读一个事件并输出其 type。返回 1=读到事件, 0=停止监听该实例 (EOF/不可恢复错误), -1=稍后重试
    int readEventType(Instance& instance, ::std::uint32_t& type)
    {
        ::std::uint8_t         buffer[kEventSize] {};
        const ::std::ptrdiff_t count { ::read(instance.fd, buffer, kEventSize) };

        if (count == static_cast<::std::ptrdiff_t>(kEventSize))
        {
            type = buffer[kEventTypeOffset];
            return 1;
        }
        if (count == 0)
        {
            return 0; // EOF: 普通文件自测场景的正常结束
        }
        if (count < 0 && (errno == EAGAIN || errno == EINTR))
        {
            return -1;
        }

        ::fprintf(stderr, "%s %s 读取异常 (%td 字节): %s\n", kTag, instance.path.c_str(), count, ::strerror(errno));
        return 0;
    }

    void printUsage(const char* program)
    {
        ::fprintf(
            stderr,
            "用法: %s <状态文件> <ep0 路径>...\n"
            "  例: %s /run/usb_bulk_holder.state /dev/usb-ffs/bulk1/ep0 "
            "/dev/usb-ffs/bulk2/ep0 /dev/usb-ffs/bulk3/ep0\n",
            program,
            program
        );
    }
} // namespace

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        printUsage(argv[0]);
        return 2;
    }

    const char*             statePath { argv[1] };
    ::std::vector<Instance> instances {};

    for (int i = 2; i < argc; ++i)
    {
        Instance instance {};
        instance.path  = argv[i];
        instance.label = labelOf(instance.path);
        // O_NONBLOCK: 事件队列空时 read 返回 EAGAIN, 而不是阻塞在 read 里
        instance.fd = ::open(instance.path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (instance.fd < 0)
        {
            ::fprintf(stderr, "%s 打开 %s 失败: %s\n", kTag, instance.path.c_str(), ::strerror(errno));
            return 1;
        }
        instances.push_back(instance);
    }

    ::std::vector<pollfd>        pollFds {};
    ::std::vector<::std::size_t> pollIndex {};
    bool                         hostConfigured { false };

    for (int pass = 0;; ++pass)
    {
        pollFds.clear();
        pollIndex.clear();
        for (::std::size_t i = 0; i < instances.size(); ++i)
        {
            if (instances[i].closed)
            {
                continue;
            }
            pollFds.push_back(pollfd { instances[i].fd, POLLIN, 0 });
            pollIndex.push_back(i);
        }
        if (pollFds.empty())
        {
            return 0; // 所有实例都已停止监听
        }

        // 第 0 轮只做一次"不等待"的排空: 在写出状态文件之前先取走已排队的事件
        // (设备实例在 holder 持有期间不会复位, 事件队列因此会跨越监听进程重启保留),
        // 避免读取方短暂看到 host_configured=0 的过渡值
        const int ready { ::poll(pollFds.data(), static_cast<nfds_t>(pollFds.size()), (pass == 0) ? 0 : kPollTimeoutMs) };
        if (ready < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            ::fprintf(stderr, "%s poll 失败: %s\n", kTag, ::strerror(errno));
            return 1;
        }

        bool changed { false };
        for (::std::size_t i = 0; i < pollFds.size(); ++i)
        {
            Instance& instance { instances[pollIndex[i]] };
            if ((pollFds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
            {
                ::fprintf(stderr, "%s %s 不再可用 (revents=0x%x)\n", kTag, instance.path.c_str(), pollFds[i].revents);
                instance.closed = true;
                continue;
            }
            if ((pollFds[i].revents & POLLIN) == 0)
            {
                continue;
            }

            // 一次就绪可能积压多个事件, 全部取走
            while (true)
            {
                ::std::uint32_t type { 0 };
                const int       result { readEventType(instance, type) };
                if (result < 0)
                {
                    break;
                }
                if (result == 0)
                {
                    instance.closed = true;
                    break;
                }

                bool enabled { instance.enabled };
                if (type == kEventEnable)
                {
                    enabled = true;
                }
                else if (type == kEventDisable || type == kEventUnbind)
                {
                    enabled = false;
                }
                else
                {
                    continue; // BIND/SETUP/SUSPEND/RESUME: 不影响"是否已配置"
                }

                if (enabled != instance.enabled)
                {
                    instance.enabled = enabled;
                    changed          = true;
                    ::fprintf(
                        stdout,
                        "%s %s: %s\n",
                        kTag,
                        instance.label.c_str(),
                        enabled ? "主机已配置该接口 (FUNCTIONFS_ENABLE)" : "主机已取消配置"
                    );
                    ::fflush(stdout);
                }
            }
        }

        if (pass == 0 || changed)
        {
            hostConfigured = false;
            for (const auto& instance : instances)
            {
                if (instance.enabled)
                {
                    hostConfigured = true;
                    break;
                }
            }
            writeStateFile(statePath, instances, hostConfigured);
        }
    }
}

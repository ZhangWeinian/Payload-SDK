#!/bin/bash
# USB Bulk 通道 gadget 配置 (树莓派侧 FunctionFS)。
#
#   PSDK 的视频/高带宽数据走 USB Bulk 通道: 树莓派作为 USB Device(peripheral),
#   飞机作为 USB Host。PSDK 侧只负责 open() 端点文件 (见 src/hal/hal_usb_bulk.h
#   的 /dev/usb-ffs/bulkN/ep{1,2}), 既不会创建 gadget, 也不会写 USB 描述符。
#
# 用法:
#   sudo bash usb_bulk_config.sh               配置 + 启动 holder + 绑定 (幂等)
#   sudo bash usb_bulk_config.sh --check       只检查状态并给出结论 (不修改)
#   sudo bash usb_bulk_config.sh --teardown    停止 holder 并清理 gadget
#   sudo bash usb_bulk_config.sh --enable-dwc2 写入 dwc2 必需启动配置 (需重启)
#
# 前置条件: 内核需处于 USB Device(peripheral) 模式, 即
#   /boot/firmware/config.txt:  dtoverlay=dwc2,dr_mode=peripheral
#   /boot/firmware/cmdline.txt: modules-load=dwc2,libcomposite   (追加在同一行!)
#   缺失时脚本会打印指引; 也可用 --enable-dwc2 自动写入 (之后需重启)。

set -u

# 与 PSDK 约定一致的固定参数 (勿随意改动)
# VID/PID 见 src/hal/hal_usb_bulk.h (LINUX_USB_VID / LINUX_USB_PID)
readonly VID='0x2ca3'
readonly PID='0xf001'
readonly GADGET_NAME='dji_psdk_bulk'
readonly CONFIGFS='/sys/kernel/config'
readonly GADGET_DIR="$CONFIGFS/usb_gadget/$GADGET_NAME"
readonly FFS_DIR='/dev/usb-ffs'
readonly FFS_INSTANCES='1 2 3' # bulk1 / bulk2 / bulk3 (对应 hal_usb_bulk.h 的 3 个接口)
readonly HOLDER_PIDFILE='/run/usb_bulk_holder.pid'
readonly HOLDER_LOG='/run/usb_bulk_holder.log'
readonly HOLDER_READY='/run/usb_bulk_holder.ready'
readonly HOLDER_FIFO='/run/usb_bulk_holder.fifo'
# 事件监听进程 (C 程序) 与其状态文件: 状态文件路径须与 application.cpp 的
# USB_BULK_HOST_STATE_FILE 一致
readonly WATCHER_BIN_NAME='usb_bulk_event_watcher'
readonly WATCHER_PIDFILE='/run/usb_bulk_watcher.pid'
readonly WATCHER_LOG='/run/usb_bulk_watcher.log'
readonly HOLDER_STATE='/run/usb_bulk_holder.state'
# 监听程序与脚本同目录发布 (构建产物 bin/ 整目录部署); libs/ 也在同一级
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly SCRIPT_DIR
WATCHER_BIN="$SCRIPT_DIR/${WATCHER_BIN_NAME}"
readonly WATCHER_BIN

log() { echo "[usb-bulk] $*"; }
warn() { echo "[usb-bulk][警告] $*" >&2; }
die() {
    echo "[usb-bulk][错误] $*" >&2
    exit 1
}

is_mounted() { grep -qs " $1 " /proc/mounts; }
udc_name() { ls /sys/class/udc 2>/dev/null | head -n 1; }
gadget_udc() { cat "$GADGET_DIR/UDC" 2>/dev/null || true; }
instance_ready() { [ -e "$FFS_DIR/bulk$1/ep1" ] && [ -e "$FFS_DIR/bulk$1/ep2" ]; }
all_instances_ready() {
    local index
    for index in $FFS_INSTANCES; do
        instance_ready "$index" || return 1
    done
    return 0
}
holder_pid() { cat "$HOLDER_PIDFILE" 2>/dev/null || true; }
holder_alive() {
    local pid
    pid="$(holder_pid)"
    [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null
}

# 列出仍持有 $FFS_DIR 下端点文件的进程 (纯 /proc 扫描, 不依赖 fuser/lsof)。
# 用途: 旧的手工实验/shell 会话持有的 fd 会让 ffs 实例无法重建, 必须先把它们找出来。
endpoints_in_use() {
    local pid fd link
    for pid in /proc/[0-9]*; do
        [ -d "$pid/fd" ] || continue
        for fd in "$pid"/fd/*; do
            link="$(readlink "$fd" 2>/dev/null)" || continue
            case "$link" in
                "$FFS_DIR"/*) printf '%s %s %s\n' "${pid#/proc/}" "$(tr -d '\0' <"$pid/comm" 2>/dev/null)" "$link" ;;
            esac
        done
    done
}

# 只自动结束身份明确的遗留物: comm 为 sleep 的孤儿进程
release_endpoints() {
    local pid fd comm link
    for pid in /proc/[0-9]*; do
        [ -d "$pid/fd" ] || continue
        link=''
        for fd in "$pid"/fd/*; do
            link="$(readlink "$fd" 2>/dev/null)" || continue
            case "$link" in
                "$FFS_DIR"/*) break ;;
                *) link='' ;;
            esac
        done
        [ -n "$link" ] || continue

        comm="$(tr -d '\0' <"$pid/comm" 2>/dev/null)"
        case "$comm" in
            sleep)
                kill "${pid#/proc/}" 2>/dev/null &&
                    log "已结束遗留的端点占用进程: pid ${pid#/proc/} (sleep, $link)"
                ;;
            *)
                warn "端点仍被占用: pid ${pid#/proc/} (${comm}, $link)"
                ;;
        esac
    done
}

# 启动配置路径 (Bookworm 起为 /boot/firmware, 更早版本为 /boot)
boot_file() {
    local name="$1"
    local path
    for path in "/boot/firmware/$name" "/boot/$name"; do
        if [ -f "$path" ]; then
            echo "$path"
            return 0
        fi
    done
    return 1
}

hint_dwc2() {
    cat >&2 <<'EOF'
[usb-bulk] 未检测到 UDC: USB 控制器没有运行在 device(peripheral) 模式, 树莓派无法作为
           USB 设备被飞机识别 (也就不会有 USB Bulk 视频通道)。需要:
             1) /boot/firmware/config.txt 增加一行: dtoverlay=dwc2,dr_mode=peripheral
             2) /boot/firmware/cmdline.txt 同一行末尾追加: modules-load=dwc2,libcomposite
                (注意: cmdline.txt 必须保持单行, 直接换行会导致无法启动)
             3) 重启树莓派; 数据线须插在树莓派的 OTG/USB-C 口 (不是普通 USB-A 口)
           可用 `sudo bash usb_bulk_config.sh --enable-dwc2` 自动写入以上两项 (需重启)。
EOF
}

# 把 FunctionFS 描述符块与空字符串块写入指定 fd (必须是已打开的 ep0)。
#
# 块格式 (内核 usb/functionfs-desc.rst, usb_functionfs_descs_head_v2):
#   magic=3(v2) | length=66 | flags=3(=HAS_FS_DESC|HAS_HS_DESC) | fs_count=3 | hs_count=3
#   随后依次是 3 个全速描述符、3 个高速描述符:
#     接口描述符: 09 04 00 00 02 FF 00 00 00  (1 接口, 2 端点, class=FF 厂商自定义)
#     EP-IN:      07 05 8N 02 <mps> 00 00    (bulk; 全速 mps=64=0x40, 高速 mps=512=0x0200)
#     EP-OUT:     07 05 0N 02 <mps> 00 00
#   端点地址与 hal_usb_bulk.h 的编号一致: bulk1→0x81/0x01, bulk2→0x82/0x02, bulk3→0x83/0x03。
#   ep 文件按描述符中端点出现的顺序命名: ep1=第一个(IN), ep2=第二个(OUT),
#   与 PSDK HAL 打开 ep1(IN)/ep2(OUT) 的行为对应。
#   注意: 两个块须各自一次 write() 写完, 且必须在同一次打开内
write_ffs_descriptors() {
    local index="$1" fd="$2"
    local ep_in ep_out
    ep_in=$(printf '\\%03o' "$((128 + index))")
    ep_out=$(printf '\\%03o' "$index")

    printf "\003\000\000\000\102\000\000\000\003\000\000\000\003\000\000\000\003\000\000\000\
\011\004\000\000\002\377\000\000\000\
\007\005${ep_in}\002\100\000\000\
\007\005${ep_out}\002\100\000\000\
\011\004\000\000\002\377\000\000\000\
\007\005${ep_in}\002\000\002\000\
\007\005${ep_out}\002\000\002\000" >&"$fd" ||
        return 1

    # 空字符串块: magic=2 | length=16 | str_count=0 | lang_count=0
    # (描述符里的字符串索引均为 0; 设备/配置描述符字符串由 composite 提供, 故无实际字符串。
    #  内核 __ffs_data_got_strings 对 str_count=lang_count=0 且 needed_count=0 是接受的)
    printf '\002\000\000\000\020\000\000\000\000\000\000\000\000\000\000\000' >&"$fd" ||
        return 1
}

# holder: 长期持有各实例的端点文件, 防止内核复位 ffs 实例

# 在 holder 进程内持有一个实例; 函数返回后 fd 仍保持打开 (fd 是进程级的)
hold_instance() {
    local index="$1"
    local ep0="$FFS_DIR/bulk${index}/ep0"
    local ep1="$FFS_DIR/bulk${index}/ep1"
    local fd_ep0 fd_ep1

    # 实例已配置 (描述符已写入, 且仍有其它持有者): 只需再持有一个端点文件
    if [ -e "$ep1" ]; then
        exec {fd_ep1}<>"$ep1" || return 1
        return 0
    fi

    # 未配置: 打开 ep0 连续写入 descriptors + strings (同一次打开, 中途不得关闭)
    exec {fd_ep0}<>"$ep0" || return 1
    if ! write_ffs_descriptors "$index" "$fd_ep0"; then
        exec {fd_ep0}>&-
        return 1
    fi

    # 描述符写完 ⇒ ep 文件出现; 改持 ep1 (数据端点, 不参与控制传输事件队列), 之后可关闭 ep0
    # (ep0 留给 usb_bulk_event_watcher 独占读事件流: 内核把事件发给唯一读者, 两边都读会互丢)
    # shellcheck disable=SC2034  # fd_ep1 只用于"保持打开", 不参与任何读写
    if ! exec {fd_ep1}<>"$ep1"; then
        exec {fd_ep0}>&-
        return 1
    fi
    exec {fd_ep0}>&-
    return 0
}

holder_main() {
    local pidfile="$1" index ok=1
    printf '%s\n' "$$" >"$pidfile" || exit 1

    for index in $FFS_INSTANCES; do
        if hold_instance "$index"; then
            log "bulk${index}: 描述符已写入, 已持有 $FFS_DIR/bulk${index}/ep1"
        else
            warn "bulk${index}: 持有失败 (ep0 写入被内核拒绝?)"
            ok=0
        fi
    done
    [ "$ok" -eq 1 ] || exit 1

    # 置就绪标记: 父进程以"标记 + 端点文件"双条件判断成功
    : >"$HOLDER_READY" 2>/dev/null || true

    log "USB Bulk 端点已全部持有 (pid $$); 持有期间实例不会被内核复位"

    # 保持 fd 打开即可, 但**不能用 sleep 之类的外部命令阻塞**: 外部命令是 fork 出来的
    # 子进程, 会继承端点 fd; holder 被杀后子进程变成孤儿继续占着实例, 于是
    # 实例无法复位、下次重建时写 UDC 会报 "No such device" (ENODEV)。
    # 这里改用 bash 内建 read 阻塞在自己创建的 FIFO 上 —— 全程不 fork 任何子进程。
    mkfifo -m 600 "$HOLDER_FIFO" 2>/dev/null || true
    exec {fd_wait}<>"$HOLDER_FIFO" || exit 1
    while read -r _ <&"$fd_wait"; do :; done
}

# 事件监听: 判断"飞机(USB Host)是否已配置我们" (FUNCTIONFS_ENABLE)
#
# dwc2 在 dr_mode=peripheral 下几乎不更新该字段
# (板测: 即使主机已复位+EnumDone 甚至完成 SET_CONFIGURATION, 它仍停在 "not attached")。
# 事件是 8 字节二进制且 type 字段带 NUL, shell 无法正确解析, 因此由随包发布的 C 程序
# usb_bulk_event_watcher 独占 ep0 读事件流, 结果写入 $HOLDER_STATE。
watcher_alive() {
    local pid
    [ -f "$WATCHER_PIDFILE" ] || return 1
    pid="$(cat "$WATCHER_PIDFILE" 2>/dev/null || true)"
    [ -n "$pid" ] || return 1
    kill -0 "$pid" 2>/dev/null
}

start_watcher() {
    local index loader
    local -a cmd=() ep0_paths=()
    stop_watcher
    : >"$WATCHER_LOG"

    if [ ! -x "$WATCHER_BIN" ]; then
        warn "未找到事件监听程序 $WATCHER_BIN"
        warn "无它则无法判断'主机是否已配置': features.usb_bulk=auto 会始终不注册 USB Bulk (可用 force 强制注册)"
        return 1
    fi

    for index in $FFS_INSTANCES; do
        ep0_paths+=("$FFS_DIR/bulk${index}/ep0")
    done

    # 与 run.sh 一致: 优先用交付目录自带的动态链接器 + libs/ (板端系统库可能过旧,
    # 直接用系统解释器会报 'GLIBC_2.36 not found' 而静默失败), 否则回退系统解释器
    for loader in "$SCRIPT_DIR/libs/ld-linux-aarch64.so.1" "$SCRIPT_DIR/libs/ld-linux-x86-64.so.2"; do
        if [ -x "$loader" ]; then
            cmd=("$loader" --library-path "$SCRIPT_DIR/libs")
            break
        fi
    done
    cmd+=("$WATCHER_BIN" "$HOLDER_STATE")

    setsid "${cmd[@]}" "${ep0_paths[@]}" </dev/null >>"$WATCHER_LOG" 2>&1 &
    printf '%s\n' "$!" >"$WATCHER_PIDFILE" || return 1
    log "已启动事件监听 (pid $!, 状态文件 $HOLDER_STATE)"
    return 0
}

stop_watcher() {
    local pid
    if [ -f "$WATCHER_PIDFILE" ]; then
        pid="$(cat "$WATCHER_PIDFILE" 2>/dev/null || true)"
        if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
            kill "$pid" 2>/dev/null || true
        fi
        rm -f "$WATCHER_PIDFILE"
    fi
    # 清理 pidfile 丢失的残留监听进程
    pkill -f -- "$WATCHER_BIN_NAME" 2>/dev/null || true
    return 0
}

stop_holder() {
    local pid waited=0
    # 状态标记与状态文件随 holder 一起作废 (事件监听进程也一并停止)
    stop_watcher
    rm -f "$HOLDER_READY" "$HOLDER_STATE"
    if [ -f "$HOLDER_PIDFILE" ]; then
        pid="$(holder_pid)"
        if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
            kill "$pid" 2>/dev/null || true
            while [ "$waited" -lt 30 ] && kill -0 "$pid" 2>/dev/null; do
                sleep 0.1
                waited=$((waited + 1))
            done
            kill -9 "$pid" 2>/dev/null || true
            log "已停止 holder (pid $pid)"
        fi
        rm -f "$HOLDER_PIDFILE"
    fi
    # 清理 pidfile 丢失的残留持有者
    pkill -f -- "--hold $HOLDER_PIDFILE" 2>/dev/null || true
    rm -f "$HOLDER_FIFO"
    return 0
}

start_holder() {
    local waited=0
    stop_holder
    : >"$HOLDER_LOG"
    rm -f "$HOLDER_READY"

    if command -v setsid >/dev/null 2>&1; then
        setsid bash "$0" --hold "$HOLDER_PIDFILE" </dev/null >>"$HOLDER_LOG" 2>&1 &
    else
        bash "$0" --hold "$HOLDER_PIDFILE" </dev/null >>"$HOLDER_LOG" 2>&1 &
    fi

    # 等 holder 写完描述符并置就绪标记 (只看 ep 文件会被上一次的残留文件骗过)
    while [ "$waited" -lt 60 ]; do
        if [ -e "$HOLDER_READY" ] && all_instances_ready; then
            return 0
        fi
        # pidfile 尚未落盘属于正常的启动竞态 (setsid/exec 需要几毫秒), 不能当成失败;
        # 只有 "pidfile 已存在但进程已消失" 才说明 holder 真的起不来
        if [ -s "$HOLDER_PIDFILE" ] && ! holder_alive; then
            return 1
        fi
        sleep 0.1
        waited=$((waited + 1))
    done
    return 1
}

# gadget 结构

check_env() {
    is_mounted "$CONFIGFS" || mount -t configfs none "$CONFIGFS" 2>/dev/null ||
        die "挂载 configfs 到 $CONFIGFS 失败"
    # [ -d /sys/module/libcomposite ] || modprobe libcomposite 2>/dev/null ||
        # die "加载 libcomposite 模块失败 (内核不支持 USB gadget?)"
    [ -n "$(udc_name)" ] || {
        hint_dwc2
        die "无可用 UDC, 无法配置 USB Bulk 通道"
    }
}

# 创建 gadget 骨架 (幂等: 只补缺失项; 必须在未绑定 UDC 时调用)
create_gadget() {
    local index
    mkdir -p "$GADGET_DIR/strings/0x409" "$GADGET_DIR/configs/c.1/strings/0x409"

    echo "$VID" >"$GADGET_DIR/idVendor"
    echo "$PID" >"$GADGET_DIR/idProduct"
    # 以下 5 项与官方《使用树莓派开发套件》快速体验 Demo 的 raspi-usb-device-start.sh 一致:
    #   bcdUSB 0x0200 —— E-Port 口为 USB 2.0; bcdDevice 0x0001;
    #   bDeviceClass/SubClass/Protocol = 0xEF/0x02/0x01 —— 多功能复合设备 (IAD)。
    # 必须在未绑定 UDC 前写入, 绑定后内核会拒绝修改设备描述符。
    echo 0x0200 >"$GADGET_DIR/bcdUSB"
    echo 0x0001 >"$GADGET_DIR/bcdDevice"
    echo 0xEF >"$GADGET_DIR/bDeviceClass"
    echo 0x02 >"$GADGET_DIR/bDeviceSubClass"
    echo 0x01 >"$GADGET_DIR/bDeviceProtocol"
    # 字符串描述符仅为可读性, 飞机侧不据此识别 (识别依据是 VID/PID 与接口/端点布局)
    echo "$(sed -n 's/^Serial.*: //p' /proc/cpuinfo | head -n 1 | tr -d ' ')" \
        >"$GADGET_DIR/strings/0x409/serialnumber"
    echo "CY" >"$GADGET_DIR/strings/0x409/manufacturer"
    echo "PSDK USB Bulk" >"$GADGET_DIR/strings/0x409/product"
    echo "PSDK Bulk" >"$GADGET_DIR/configs/c.1/strings/0x409/configuration"
    # bmAttributes 0x80: D7 保留位须置 1, 其余为 0 ⇒ 总线供电
    echo 0x80 >"$GADGET_DIR/configs/c.1/bmAttributes"
    echo 250 >"$GADGET_DIR/configs/c.1/MaxPower"

    for index in $FFS_INSTANCES; do
        mkdir -p "$GADGET_DIR/functions/ffs.bulk${index}"
        if [ ! -L "$GADGET_DIR/configs/c.1/ffs.bulk${index}" ]; then
            # 必须用绝对路径: configfs 通过 kern_path() 按"进程 cwd"解析链接目标,
            # 写 ../../functions/... 会解析到 configfs 之外 ⇒ ENOENT
            ln -s "$GADGET_DIR/functions/ffs.bulk${index}" "$GADGET_DIR/configs/c.1/" ||
                die "链接 ffs.bulk${index} 到配置失败"
        fi
    done
}

# 挂载 3 个 FunctionFS 实例 (幂等; 实例名必须与 functions/ffs.bulkN 的后缀一致)
mount_ffs() {
    local index
    for index in $FFS_INSTANCES; do
        mkdir -p "$FFS_DIR/bulk${index}"
        is_mounted "$FFS_DIR/bulk${index}" && continue
        # 挂载参数与官方脚本一致: 端点文件对普通用户可读写, 便于现场非 root 排查
        # (此前默认 0500/0600 仅 root 可见, 非 root 下 glob 展开失败会误报"文件不存在")
        mount -o mode=0777,uid=2000,gid=2000 -t functionfs "bulk${index}" "$FFS_DIR/bulk${index}" ||
            die "挂载 functionfs 失败: $FFS_DIR/bulk${index}"
    done
}

bind_udc_if_needed() {
    local udc
    [ -n "$(gadget_udc)" ] && return 0
    udc="$(udc_name)"
    if ! echo "$udc" >"$GADGET_DIR/UDC" 2>/dev/null; then
        # ENODEV 的唯一来源是 _ffs_func_bind: ffs_opts->dev->desc_ready ? 0 : -ENODEV,
        # 即"正在绑定的这个函数实例还没有写入描述符" (描述符写进了别的实例/旧挂载)。
        warn "写 UDC 失败: 内核拒绝绑定 (ENODEV/EBUSY 等)。"
        warn "最可能原因: 描述符没有写进当前这个实例 (存在悬空挂载或残留占用进程)。"
        dmesg 2>/dev/null | tail -n 5 >&2 || true
        die "绑定 UDC ($udc) 失败"
    fi
    log "已绑定 UDC: $udc"
}

# 目录存在才删除; 删除失败明确告警 (configfs/gadget 目录被占用时无法删除)
rmdir_checked() {
    [ -e "$1" ] || return 0
    if rmdir "$1" 2>/dev/null; then
        return 0
    fi
    warn "删除失败: $1 (仍被进程占用?)"
    return 1
}

# 删除 gadget 目录树 (configfs 要求先删软链与子目录, 顺序: 链接 -> 函数 -> 配置 -> 字符串 -> gadget)
remove_gadget_dirs() {
    local index failed=0

    for index in $FFS_INSTANCES; do
        if [ -L "$GADGET_DIR/configs/c.1/ffs.bulk${index}" ]; then
            rm -f "$GADGET_DIR/configs/c.1/ffs.bulk${index}" || failed=1
        fi
        rmdir_checked "$GADGET_DIR/functions/ffs.bulk${index}" || failed=1
        rmdir_checked "$FFS_DIR/bulk${index}" || failed=1
    done
    rmdir_checked "$GADGET_DIR/configs/c.1/strings/0x409" || failed=1
    rmdir_checked "$GADGET_DIR/configs/c.1" || failed=1
    rmdir_checked "$GADGET_DIR/strings/0x409" || failed=1
    rmdir_checked "$GADGET_DIR" || failed=1

    return "$failed"
}

# 重建前的清理。为什么必须做干净:
#   FunctionFS 的挂载与 configfs 函数实例(ffs.bulkN)是一一配对的。若上一次的挂载没能卸载
#   (例如被别的进程占着端点), 这个挂载就变成"悬空"状态: holder 仍能把描述符写进它, ep 文件
#   也会出现, 但它对应的是旧实例; 而 gadget 绑定的是刚 mkdir 出来的新实例 —— 新实例
#   desc_ready 仍为 false, 于是写 UDC 时内核在 _ffs_func_bind 返回 -ENODEV
#   (f_fs.c: ret = ffs_opts->dev->desc_ready ? 0 : -ENODEV), 现象就是
#   "echo: write error: No such device"。
cleanup_stale_state() {
    local index inuse waited=0

    stop_holder

    # 先让遗留进程释放端点 (自动结束旧版 holder 的孤儿 sleep; 其余只告警),
    # 并把仍被占用的进程打出来 —— 否则下面 umount 必然失败, 而且报错来得太晚
    release_endpoints

    # 其它进程(例如上次手工实验遗留的 shell 会话)仍持有端点时, 实例无法被安全重建。
    # 刚被杀掉的 holder 也可能需要一点点时间释放 fd, 所以留一个重试窗口。
    while [ "$waited" -lt 20 ]; do
        inuse="$(endpoints_in_use)"
        [ -z "$inuse" ] && break
        sleep 0.1
        waited=$((waited + 1))
    done
    if [ -n "$inuse" ]; then
        warn "以下进程仍占用 $FFS_DIR 下的端点文件:"
        echo "$inuse" >&2
        warn "处理方式: 若上面是 bash/shell 会话(手工实验遗留), 切换到该会话执行 exit;"
        warn "          若上面是 cy_psdk, 请先停止应用; 也可直接 sudo kill <pid> 后重试。"
        die "端点被其它进程占用, 无法重建 USB Bulk 通道"
    fi

    if [ -n "$(gadget_udc)" ]; then
        log "解绑旧的 UDC 绑定"
        echo "" >"$GADGET_DIR/UDC" 2>/dev/null || die "解绑 UDC 失败"
        sleep 0.2
    fi

    for index in $FFS_INSTANCES; do
        if is_mounted "$FFS_DIR/bulk${index}"; then
            umount "$FFS_DIR/bulk${index}" 2>/dev/null ||
                die "卸载 $FFS_DIR/bulk${index} 失败 (仍有进程占用端点?), 请先结束占用进程"
            log "已卸载 $FFS_DIR/bulk${index}"
            sleep 0.1
        fi
    done

    if [ -d "$GADGET_DIR" ]; then
        log "删除旧的 gadget 结构, 使函数实例与挂载重新配对"
        remove_gadget_dirs || die "旧的 gadget 结构未清理干净, 请处理上方失败项后重试"
    fi
}

setup() {
    [ "$(id -u)" -eq 0 ] || die "需要 root 权限 (configfs/挂载/绑定 UDC), 请用 sudo 运行"

    check_env

    if all_instances_ready && holder_alive; then
        if [ -z "$(gadget_udc)" ]; then
            # 端点已在 (holder 活着), 只差绑定
            bind_udc_if_needed
        fi
        # 监听进程不在时补起 (ffs 的事件队列在实例存活期间会保留, 重启监听不会丢已发生的事件)
        watcher_alive || start_watcher || true
        log "USB Bulk 通道已就绪 (holder pid $(holder_pid), udc $(gadget_udc))"
        return 0
    fi

    # 需要(重新)写描述符: 先彻底清理旧状态, 否则会出现"描述符写进旧实例、gadget 绑定新实例"的
    # 失配 (症状: 写 UDC 报 No such device)
    cleanup_stale_state

    create_gadget
    mount_ffs

    if ! start_holder; then
        warn "端点持有进程未就绪, 日志 ($HOLDER_LOG) 尾部:"
        tail -n 20 "$HOLDER_LOG" >&2 2>/dev/null || true
        die "USB Bulk 端点未就绪 (见上方 holder 日志)"
    fi

    # 先起监听再绑定 UDC: 这样绑定/主机配置产生的事件不会错过
    start_watcher || true

    bind_udc_if_needed

    all_instances_ready || die "配置完成但端点文件未出现, 请检查内核 FunctionFS 支持"
    log "USB Bulk 通道就绪: $FFS_DIR/bulk{1,2,3}/ep{1,2} (等待飞机侧枚举)"
}

check() {
    local ok=1 index host_configured='?' detail=''
    echo "===== USB Bulk 通道状态 ====="

    if [ -d "$CONFIGFS" ] && is_mounted "$CONFIGFS"; then
        echo "[ok]   configfs 已挂载: $CONFIGFS"
    else
        echo "[--]   configfs 未挂载 (执行本脚本会自动挂载)"
        ok=0
    fi

    if [ -d /sys/module/libcomposite ]; then
        echo "[ok]   libcomposite 已加载"
    else
        echo "[--]   libcomposite 未加载"
        ok=0
    fi

    local udc
    udc="$(udc_name)"
    if [ -n "$udc" ]; then
        echo "[ok]   UDC 可用: $udc"
    else
        echo "[--]   无 UDC: USB 控制器不在 device(peripheral) 模式"
        ok=0
    fi

    if [ -n "$(gadget_udc)" ]; then
        echo "[ok]   gadget 已绑定: $(gadget_udc)"
    else
        echo "[--]   gadget 未绑定 (执行本脚本可完成配置)"
        ok=0
    fi

    # 设备描述符: 与官方脚本对齐的 5 个字段, 枚举异常时先看这里
    if [ -r "$GADGET_DIR/bcdUSB" ]; then
        echo "[ok]   设备描述符: bcdUSB=$(cat "$GADGET_DIR/bcdUSB") bcdDevice=$(cat "$GADGET_DIR/bcdDevice") class=$(cat "$GADGET_DIR/bDeviceClass")/$(cat "$GADGET_DIR/bDeviceSubClass")/$(cat "$GADGET_DIR/bDeviceProtocol") bmAttributes=$(cat "$GADGET_DIR/configs/c.1/bmAttributes" 2>/dev/null || echo '?')"
    fi

    if holder_alive; then
        echo "[ok]   端点持有进程存活 (pid $(holder_pid))"
    else
        echo "[--]   端点持有进程不在: ep 文件会随最后一个 fd 关闭被内核销毁"
        ok=0
    fi

    if watcher_alive; then
        echo "[ok]   事件监听进程存活 (pid $(cat "$WATCHER_PIDFILE"))"
    else
        echo "[--]   事件监听进程不在: 无法判断'主机是否已配置', 状态文件可能过期"
        ok=0
    fi

    for index in $FFS_INSTANCES; do
        if instance_ready "$index"; then
            echo "[ok]   bulk${index} 端点就绪: ep1(IN) ep2(OUT)"
        else
            echo "[--]   bulk${index} 端点未就绪 (描述符未写入或 ffs 已复位)"
            ok=0
        fi
    done

    # 飞机侧是否已配置我们 = 唯一可靠判据 (不能用 /sys/class/udc/*/state: dwc2 在
    # peripheral 模式下几乎不更新它, 恒为 "not attached"); 信号来自 usb_bulk_event_watcher
    # 对 ep0 事件流 (FUNCTIONFS_ENABLE) 的监听
    if [ -r "$HOLDER_STATE" ]; then
        host_configured="$(sed -n 's/^host_configured=//p' "$HOLDER_STATE" | head -n 1)"
        detail="$(sed -n 's/^/ /p' "$HOLDER_STATE" | tr -d '\n')"
    fi
    if [ "$host_configured" = 1 ]; then
        echo "[ok]   主机已配置 (FUNCTIONFS_ENABLE): 是$detail"
    else
        echo "[--]   主机已配置 (FUNCTIONFS_ENABLE): 否$detail"
        echo "       飞机未对本设备发出 SET_CONFIGURATION ⇒ 该链路无法承载数据"
        echo "       核对: 飞机已上电启动 / E-Port 开发板 USB 主从拨码=Host / 同轴线 A-B 面 / 标识5 用 USB-A 转 USB-C 接树莓派 Type-C"
        ok=0
    fi

    if [ -n "$udc" ] && [ "$ok" -eq 1 ]; then
        echo "结论: USB Bulk 通道可用 (视频/高带宽数据还需高级许可 + 飞机侧取流)"
        return 0
    fi

    echo "结论: USB Bulk 通道不可用"
    if [ -z "$udc" ]; then
        # 最常见原因: 内核没切到 device 模式, 此时配置 gadget 也无从依附
        hint_dwc2
    else
        echo "      运行 'sudo bash $(basename "$0")' 完成配置"
    fi
    return 1
}

teardown() {
    [ "$(id -u)" -eq 0 ] || die "需要 root 权限, 请用 sudo 运行"

    stop_holder

    # 先解决"谁占着端点"这件事: 自动结束旧版 holder 的孤儿 sleep, 并把仍被占用的进程
    # 立刻打印出来 —— 放在最前面, 免得后续 umount 报错刷屏后关键信息被冲掉或被打断
    release_endpoints

    if [ -d "$GADGET_DIR" ]; then
        if [ -n "$(gadget_udc)" ]; then
            # configfs 要求先解绑, 再删结构与挂载
            echo "" >"$GADGET_DIR/UDC" 2>/dev/null || warn "解绑 UDC 失败"
            sleep 0.2
        fi

        local index
        for index in $FFS_INSTANCES; do
            if is_mounted "$FFS_DIR/bulk${index}"; then
                # 卸载失败必须明确告警: 悬空挂载会在下次重建时造成实例失配 (写 UDC 报 ENODEV)
                umount "$FFS_DIR/bulk${index}" 2>/dev/null ||
                    warn "卸载 $FFS_DIR/bulk${index} 失败: 仍有进程占用端点"
                sleep 0.1
            fi
        done

        if remove_gadget_dirs; then
            log "已清理 gadget: $GADGET_NAME"
        else
            warn "gadget 未完全清理 (见上方失败项), 请结束占用 $FFS_DIR 的进程后重试"
        fi
    else
        log "gadget 不存在, 无需清理"
    fi

    # 告知仍未释放端点的进程 (下一次重建会因此失败)
    local inuse
    inuse="$(endpoints_in_use)"
    if [ -n "$inuse" ]; then
        warn "仍有进程占用 $FFS_DIR 下的端点 (下次重建可能失败):"
        echo "$inuse" >&2
    fi
}

# 写入 dwc2 启动配置 (幂等, 带 .bak 备份; 重启后生效)
enable_dwc2() {
    [ "$(id -u)" -eq 0 ] || die "需要 root 权限, 请用 sudo 运行"

    local cfg cmdline
    cfg="$(boot_file config.txt)" || die "未找到 config.txt (/boot/firmware 或 /boot)"
    cmdline="$(boot_file cmdline.txt)" || die "未找到 cmdline.txt (/boot/firmware 或 /boot)"
    log "目标文件: $cfg / $cmdline"

    if grep -q '^dtoverlay=dwc2,dr_mode=peripheral' "$cfg"; then
        log "config.txt 已配置 dtoverlay=dwc2,dr_mode=peripheral, 跳过"
    else
        [ -f "$cfg.bak.usb-bulk" ] || cp "$cfg" "$cfg.bak.usb-bulk"
        # 注释掉与 peripheral 冲突的既有配置 (dr_mode=host 与不带参数的 dtoverlay=dwc2)
        sed -i 's|^dtoverlay=dwc2,dr_mode=host|#& (PSDK USB Bulk: 与 dr_mode=peripheral 冲突)|' "$cfg"
        sed -i 's|^dtoverlay=dwc2$|#& (PSDK USB Bulk: 由下方 dr_mode=peripheral 取代)|' "$cfg"
        printf '# PSDK USB Bulk: 树莓派 USB 口作为设备(peripheral)被飞机识别\ndtoverlay=dwc2,dr_mode=peripheral\n' >>"$cfg"
        log "已写入 dtoverlay=dwc2,dr_mode=peripheral, 原 host 模式配置已注释"
    fi

    if grep -q 'modules-load=dwc2,libcomposite' "$cmdline"; then
        log "cmdline.txt 已包含 modules-load=dwc2,libcomposite, 跳过"
    else
        [ -f "$cmdline.bak.usb-bulk" ] || cp "$cmdline" "$cmdline.bak.usb-bulk"
        # cmdline.txt 必须是单行: 只能追加到第一行行尾
        sed -i '1 s/$/ modules-load=dwc2,libcomposite/' "$cmdline"
        log "已向 cmdline.txt 第一行行尾追加: modules-load=dwc2,libcomposite"
    fi

    log "配置已写入, 请重启树莓派后重新运行本脚本 (不带参数) 完成 gadget 配置"
}

usage() {
    # 用法段 = 文件头注释块 (第 2 行到 set -u 之前), 与行号解耦以免改动头部后失效
    sed -n '2,/^set -u$/p' "$0" | sed 's/^# \{0,1\}//; /^set -u$/d; /^$/d'
}

case "${1:---setup}" in
    --setup) setup ;;
    --check) check ;;
    --teardown) teardown ;;
    --enable-dwc2) enable_dwc2 ;;
    --hold) holder_main "${2:?hold 模式需要 pidfile 参数}" ;;
    -h | --help) usage ;;
    *)
        usage
        exit 2
        ;;
esac

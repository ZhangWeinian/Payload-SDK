#!/usr/bin/env python3
"""PSDK 程序 MQTT 链路测试客户端 (开发容器内使用, 依赖 paho-mqtt)。

用途: 在容器内直接跑 x86 版 cy_psdk 时, 下发报文并观测上行, 验证
      "MQTT 路由 → JSON 解析 → KMZ 构建" 全流程, 不依赖 MQTTX 等外部工具。

用法:
  mqtt_test_client.py watch [秒数]               被动观测全部主题 (默认 30s, 不下发任何指令)
  mqtt_test_client.py task  [观测秒数]           下发样例航线任务 (XFHXRW), 之后观测上行
  mqtt_test_client.py cmd   <XXLX> [观测秒数]    下发指令飞行命令 (QF 起飞 / FH 返航 / XT 悬停 / JL 降落)
  mqtt_test_client.py raw   <topic> <json>       下发任意报文

可选参数:
  --broker HOST:PORT   默认 192.168.1.112:1883
  --zbid   ZBID        默认从 config/config.yml 的 plane.code 读取 (必须与程序一致, 否则被忽略)

两个必须知道的坑:
  1) 本程序主题以斜杠开头 ("/wrgk/uav/...")。MQTT 的 "wrgk/#" **匹配不到** "/wrgk/...",
     订阅必须用 "#" 或 "/wrgk/#", 否则一条报文都收不到。
  2) ZBID 必须严格等于程序的生效身份 (配置 plane.code 优先, 否则为飞控序列号)。
     不匹配时程序会打 DEBUG "收到发往其他设备 (...) 的消息, 本机 (...) 已忽略" 并丢弃。
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys
import time

try:
    import paho.mqtt.client as mqtt
except ImportError:  # pragma: no cover
    sys.exit(
        "缺少 paho-mqtt: uv pip install --target /home/vscode/.local/lib/python-global-packages paho-mqtt"
    )

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
SAMPLE_TASK = (
    REPO_ROOT
    / "samples/sample_c++/platform/linux/cy_psdk/test/sample_data/kmz_template.json"
)
APP_CONFIG = REPO_ROOT / "config/config.yml"

TOPIC_MISSION_CONTROL = "/wrgk/uav/mission_control"  # XXLX=XFHXRW
TOPIC_COMMAND_CONTROL = "/wrgk/uav/command_control"  # XXLX=QF/FH/XT/JL

HEADER_KEYS = ("ZBID", "XXID", "XXLX", "SJC", "RWID")


def read_configured_zbid(config_path: pathlib.Path | None = None) -> str:
    """从 config.yml 读取 plane.code (程序的身份来源, 必须与报文 ZBID 一致)。

    注意: 必须指向**程序实际加载的那份 config.yml** (通常是可执行文件所在目录),
    而不是仓库里的模板 —— 两份不一致时下发会被程序当"发往其他设备"丢弃。
    """
    try:
        import yaml  # PyYAML 在全局 pydeps 中

        with (config_path or APP_CONFIG).open(encoding="utf-8") as fh:
            cfg = yaml.safe_load(fh)
        return str((cfg.get("plane") or {}).get("code") or "")
    except Exception:
        return ""


def brief(payload: bytes) -> str:
    text = payload.decode("utf-8", "replace")
    try:
        j = json.loads(text)
        if isinstance(j, dict):
            return json.dumps(
                {k: j[k] for k in HEADER_KEYS if k in j}, ensure_ascii=False
            )
    except Exception:
        pass
    return text[:160].replace("\n", " ")


class Client:
    def __init__(self, broker: str, port: int) -> None:
        self.counts: dict[str, int] = {}
        self.samples: list[str] = []
        self.client = mqtt.Client(
            mqtt.CallbackAPIVersion.VERSION2, client_id=f"mqtt-test-{int(time.time())}"
        )
        self.client.on_connect = self._on_connect
        self.client.on_message = self._on_message
        self.broker = broker
        self.port = port

    def _on_connect(self, _c, _u, _flags, rc, _props=None) -> None:
        print(f"[连接] broker={self.broker}:{self.port} rc={rc}")
        # 必须 "#": 主题带前导斜杠, "wrgk/#" 收不到
        self.client.subscribe("#", qos=1)

    def _on_message(self, _c, _u, msg) -> None:
        self.counts[msg.topic] = self.counts.get(msg.topic, 0) + 1
        line = f"[{time.strftime('%H:%M:%S')}] {msg.topic}  {brief(msg.payload)}"
        self.samples.append(line)
        print(line, flush=True)

    def start(self) -> None:
        self.client.connect(self.broker, self.port, 20)
        self.client.loop_start()
        time.sleep(1.0)  # 等订阅生效

    def publish(self, topic: str, payload: str) -> None:
        info = self.client.publish(topic, payload, qos=1)
        info.wait_for_publish(timeout=5)
        print(
            f"[已下发] {topic}\n         {payload[:160]}{'...' if len(payload) > 160 else ''}"
        )

    def observe(self, seconds: int) -> None:
        print(f"[观测] {seconds}s ...")
        time.sleep(max(1, seconds))
        self.client.loop_stop()
        self.client.disconnect()
        print("\n=== 各主题报文统计 ===")
        for topic, n in sorted(self.counts.items(), key=lambda kv: -kv[1]):
            print(f"  {n:5d}  {topic}")
        if not self.counts:
            print("  (观察窗口内未收到任何报文)")


def main() -> int:
    parser = argparse.ArgumentParser(description="PSDK 程序 MQTT 链路测试客户端")
    parser.add_argument("action", choices=["watch", "task", "cmd", "raw"])
    parser.add_argument("arg1", nargs="?", default="")
    parser.add_argument("arg2", nargs="?", default="")
    parser.add_argument("args", nargs="*", default=[])
    parser.add_argument("--broker", default="192.168.1.112:1883")
    parser.add_argument("--zbid", default="")
    parser.add_argument(
        "--config",
        default="",
        help="程序实际加载的 config.yml 路径 (用于取其 plane.code)",
    )
    parser.add_argument("--seconds", type=int, default=15, help="下发后观测上行的秒数")
    opts = parser.parse_args()

    host, _, port_str = opts.broker.partition(":")
    zbid = opts.zbid or read_configured_zbid(
        pathlib.Path(opts.config) if opts.config else None
    )
    if not zbid:
        print("[警告] 未能确定 ZBID (config.yml 的 plane.code 为空且未指定 --zbid)")
    else:
        print(f"[身份] ZBID = {zbid}")

    cli = Client(host, int(port_str or 1883))
    cli.start()

    if opts.action == "watch":
        cli.observe(int(opts.arg1) if opts.arg1 else 30)
        return 0

    if opts.action == "task":
        raw = SAMPLE_TASK.read_text(encoding="utf-8")
        payload = json.loads(raw)
        payload["ZBID"] = zbid
        payload.setdefault("XXID", f"test-{int(time.time())}")
        cli.publish(TOPIC_MISSION_CONTROL, json.dumps(payload, ensure_ascii=False))
        cli.observe(int(opts.arg1) if opts.arg1 else opts.seconds)
        return 0

    if opts.action == "cmd":
        xxlx = (opts.arg1 or "").upper()
        if xxlx not in {"QF", "FH", "XT", "JL"}:
            return print("用法: cmd <QF|FH|XT|JL>") or 2
        payload = {
            "ZBID": zbid,
            "XXID": f"test-{int(time.time())}",
            "XXLX": xxlx,
            "SJC": int(time.time() * 1000),
            "XXXX": {},
        }
        cli.publish(TOPIC_COMMAND_CONTROL, json.dumps(payload, ensure_ascii=False))
        cli.observe(int(opts.arg2) if opts.arg2 else opts.seconds)
        return 0

    # raw
    if not opts.arg1:
        return print("用法: raw <topic> <json>") or 2
    topic = opts.arg1
    if not topic.startswith("/"):
        print(
            f"[提醒] 该主题不以斜杠开头 ('{topic}'), 程序注册的是 '/wrgk/uav/...' 形式, 很可能收不到"
        )
    cli.publish(topic, opts.arg2)
    cli.observe(opts.seconds)
    return 0


if __name__ == "__main__":
    sys.exit(main())

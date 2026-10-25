"""物理UART0配网工具：不把Wi-Fi密码放进源码、参数或输出日志。"""
import argparse
import getpass
import json
import sys
import time
from pathlib import Path

import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM25")
    parser.add_argument("--ssid", required=True)
    parser.add_argument("--host", required=True, help="电脑局域网IPv4或Broker主机名")
    parser.add_argument("--mqtt-port", type=int, default=18830)
    parser.add_argument("--token-file", type=Path, required=True)
    parser.add_argument("--password-stdin", action="store_true", help="自动化时从标准输入读取密码")
    parser.add_argument("--allow-legacy-wpa", action="store_true", help="仅为已确认的旧WPA1路由器启用；默认要求WPA2")
    args = parser.parse_args()
    password = sys.stdin.readline().rstrip("\r\n") if args.password_stdin else getpass.getpass("Wi-Fi password: ")
    token = args.token_file.read_text(encoding="utf-8").strip()
    if not 0 < len(args.ssid.encode("utf-8")) <= 32 or len(password.encode("utf-8")) > 64:
        raise SystemExit("SSID/password byte length is invalid")
    request = {"ssid": args.ssid, "password": password, "host": args.host,
               "port": args.mqtt_port, "token": token, "allow_legacy_wpa": args.allow_legacy_wpa}
    wire = ("NETCFG " + json.dumps(request, ensure_ascii=False, separators=(",", ":")) + "\n").encode("utf-8")
    if len(wire) > 512:
        raise SystemExit("Configuration line is too large")
    port = serial.Serial()
    port.port, port.baudrate, port.timeout = args.port, 115200, 0.2
    port.dtr = port.rts = False
    try:
        port.open()
        port.reset_input_buffer()
        port.write(wire)
        port.flush()
        deadline = time.monotonic() + 12
        while time.monotonic() < deadline:
            line = port.readline().decode("utf-8", errors="replace")
            if "NETCFG saved;" in line:
                print("Network configuration saved; ESP32 is restarting. Credentials not displayed.")
                return
            if "NETCFG rejected:" in line:
                raise SystemExit("ESP32 rejected the configuration (credentials not displayed)")
        raise SystemExit("No configuration acknowledgement. Check UART0 connection and firmware version.")
    finally:
        port.close()


if __name__ == "__main__":
    main()

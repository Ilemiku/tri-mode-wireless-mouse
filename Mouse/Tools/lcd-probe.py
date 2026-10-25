"""ZE精英板FSMC_NE4/A10屏幕ID探测。短暂暂停CPU，最终恢复GPIO/FSMC配置。"""
import json
import time

from pyocd.core.helpers import ConnectHelper


def main():
    session = ConnectHelper.session_with_chosen_probe(options={
        "target_override": "cortex_m", "connect_mode": "attach"})
    session.open()
    target = session.target
    saved = {}
    try:
        target.halt()

        def change(address, value):
            if address not in saved:
                saved[address] = target.read32(address)
            target.write32(address, value)

        change(0x40021018, target.read32(0x40021018) | (1 << 5) | (1 << 6) | (1 << 8))
        change(0x40021014, target.read32(0x40021014) | (1 << 8))
        pins = {0x40011400: [0, 1, 4, 5, 8, 9, 10, 14, 15],
                0x40011800: list(range(7, 16)), 0x40012000: [0, 12]}
        for port, numbers in pins.items():
            for pin in numbers:
                address = port + (0 if pin < 8 else 4)
                shift = (pin & 7) * 4
                change(address, (target.read32(address) & ~(15 << shift)) | (11 << shift))
        change(0xA0000018, 0)
        change(0xA000001C, (31 << 8) | 7)
        change(0xA000011C, (7 << 8) | 3)
        change(0xA0000018, (1 << 14) | (1 << 12) | (1 << 4) | 1)
        time.sleep(0.02)
        result = {}
        for command in (0xD3, 0x04, 0xD4, 0xA1):
            target.write16(0x6C000000, command)
            result[f"0x{command:02X}"] = [f"0x{target.read16(0x6C000800):04X}" for _ in range(4)]
        print(json.dumps(result))
    finally:
        # 先恢复FSMC与GPIO，最后恢复时钟使能，不留下本次探测的总线配置。
        for address, value in reversed(list(saved.items())):
            target.write32(address, value)
        target.resume()
        session.close()


if __name__ == "__main__":
    main()

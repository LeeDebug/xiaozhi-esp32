# -*- coding: utf-8 -*-
"""
Environment X6 Sensor 数据解析 / 串口采集工具
================================================
适用链路：USB转485(中盛CH340) → 485转TTL(智胜BY-TTL-485) → Environment X6(微雪)

帧结构（22 字节，大端）：
  [0]      0x70            指令头
  [1:5]    IEEE754 float   IAQ
  [5:9]    IEEE754 float   TVOC   (ppm)
  [9:13]   IEEE754 float   HCHO   (ppm)
  [13:17]  IEEE754 float   CO     (ppm)
  [17:19]  uint16  /100    温度   (℃)
  [19:21]  uint16  /100    湿度   (%RH)
  [21]     uint8           校验和 0-add8（前面 21 字节求和 → 取低 8 位 → 取反 +1）

用法：
  1) 解析 SSCOM 里复制出来的十六进制帧
     python x6_sensor.py --hex "70 42 3E 00 00 3D C1 C2 3B 3C 44 9B A6 3D 65 60 42 0B 5E 18 FC 93"

  2) 直接连串口实时读取（需要 pyserial：pip install pyserial）
     python x6_sensor.py --port COM12
     python x6_sensor.py --port COM12 --interval 2 --csv log.csv
"""

import argparse
import struct
import sys
import time

FRAME_LEN = 22
HEADER = 0x70
QUERY = bytes([0x70, 0x90])

IAQ_LEVELS = [
    (50, "良好"), (100, "中等"), (150, "对敏感人群不健康"),
    (200, "不健康"), (300, "非常不健康"), (500, "危险"),
]


def add8(data):
    """0-add8 校验：求和取低 8 位，按位取反再加 1。"""
    return (-sum(data)) & 0xFF


def iaq_level(v):
    for limit, name in IAQ_LEVELS:
        if v <= limit:
            return name
    return "超量程"


def parse_frame(frame):
    """解析 22 字节帧，返回 dict；长度或帧头不对返回 None。"""
    if len(frame) != FRAME_LEN or frame[0] != HEADER:
        return None
    iaq, tvoc, hcho, co = struct.unpack(">ffff", frame[1:17])
    temp = int.from_bytes(frame[17:19], "big") / 100.0
    hum = int.from_bytes(frame[19:21], "big") / 100.0
    return {
        "iaq": iaq,
        "tvoc": tvoc,
        "hcho": hcho,
        "co": co,
        "temp": temp,
        "hum": hum,
        "checksum_ok": add8(frame[:21]) == frame[21],
        "checksum_calc": add8(frame[:21]),
        "checksum_got": frame[21],
    }


def fmt(d):
    ok = "校验OK " if d["checksum_ok"] else "校验失败"
    return (
        f'IAQ {d["iaq"]:6.1f} ({iaq_level(d["iaq"])})   '
        f'TVOC {d["tvoc"]:.4f} ppm   HCHO {d["hcho"]:.4f} ppm   CO {d["co"]:.4f} ppm   '
        f'{d["temp"]:.2f} ℃   {d["hum"]:.2f} %RH   [{ok}]'
    )


def show(hexstr, label=""):
    try:
        frame = bytes.fromhex(hexstr.replace(" ", "").replace("\n", "").replace("0x", ""))
    except ValueError:
        print(f"{label}解析失败：不是合法的十六进制（可能从截图复制时掉了字节）")
        return
    d = parse_frame(frame)
    if d is None:
        print(f"{label}不是有效帧：长度 {len(frame)}（应为 22），"
              f"帧头 0x{frame[0]:02X}（应为 0x70）")
        return
    print(f"{label}{fmt(d)}")


def find_frames(buf):
    """从字节流里切出所有校验通过的完整帧，剩下的尾巴留着下次续上。"""
    frames, i = [], 0
    while i + FRAME_LEN <= len(buf):
        if buf[i] == HEADER and add8(buf[i:i + 21]) == buf[i + 21]:
            frames.append(bytes(buf[i:i + FRAME_LEN]))
            i += FRAME_LEN
        else:
            i += 1
    return frames, buf[i:]


def run_serial(port, interval, csv_path, count):
    try:
        import serial
    except ImportError:
        sys.exit("缺少 pyserial，请先执行：pip install pyserial")

    ser = serial.Serial(port=port, baudrate=9600, bytesize=8,
                        parity="N", stopbits=1, timeout=0.5)
    print(f"已打开 {port} @9600 8N1，开始轮询（Ctrl+C 退出）\n")

    fh = None
    if csv_path:
        fh = open(csv_path, "w", encoding="utf-8-sig")
        fh.write("时间,IAQ,TVOC_ppm,HCHO_ppm,CO_ppm,温度C,湿度RH\n")

    buf = bytearray()
    n = 0
    try:
        while count == 0 or n < count:
            ser.reset_input_buffer()
            ser.write(QUERY)
            deadline = time.time() + 1.0
            while time.time() < deadline:
                buf += ser.read(64)
                frames, buf = find_frames(buf)
                if frames:
                    d = parse_frame(frames[0])
                    ts = time.strftime("%H:%M:%S")
                    print(f"[{ts}] {fmt(d)}")
                    if fh:
                        fh.write(f'{ts},{d["iaq"]:.1f},{d["tvoc"]:.4f},'
                                 f'{d["hcho"]:.4f},{d["co"]:.4f},{d["temp"]:.2f},{d["hum"]:.2f}\n')
                        fh.flush()
                    n += 1
                    break
            else:
                print("[超时] 没收到完整帧 —— 检查 A/B、RX/TX 与供电")
            time.sleep(max(interval - 1.0, 0))
    except KeyboardInterrupt:
        print("\n已停止")
    finally:
        ser.close()
        if fh:
            fh.close()
            print(f"数据已保存到 {csv_path}")


def main():
    ap = argparse.ArgumentParser(description="Environment X6 Sensor 帧解析 / 采集")
    ap.add_argument("--hex", help="解析一段十六进制帧，如 \"70 42 3E 00 00 ...\"")
    ap.add_argument("--port", help="串口号，如 COM12")
    ap.add_argument("--interval", type=float, default=1.0, help="轮询间隔秒，默认 1")
    ap.add_argument("--csv", help="把读数写入 CSV 文件")
    ap.add_argument("--count", type=int, default=0, help="采集多少条后停止，0=一直采")
    args = ap.parse_args()

    if args.hex:
        show(args.hex)
    elif args.port:
        run_serial(args.port, args.interval, args.csv, args.count)
    else:
        # 没给参数就跑两个自带样例，方便确认脚本可用
        print("未指定参数，运行内置样例：\n")
        show("70 42 30 00 00 3D 35 64 F0 3D 53 D2 5B 3D 08 19 55 0A C8 27 0F E0",
             "官方文档示例  ")
        show("70 42 3E 00 00 3D C1 C2 3B 3C 44 9B A6 3D 65 60 42 0B 5E 18 FC 93",
             "你的实测数据  ")
        print("\n用法： python x6_sensor.py --hex \"70 ...\"    或    --port COM12")


if __name__ == "__main__":
    main()

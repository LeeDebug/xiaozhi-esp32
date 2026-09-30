# -*- coding: utf-8 -*-
"""
Environment X6 Sensor（微雪 SKU 34169）串口协议编解码 + 全部 API 封装
=====================================================================
参考文档：https://docs.waveshare.net/Environment_X6_Sensor/

通信参数
--------
- UART，波特率 9600，数据位 8，停止位 1，无校验，无流控
- 字节序：大端（高字节在前）
- 帧格式：指令头 + Data + 校验和
- 校验和算法：0-add8（所有字节相加取低 8 位，按位取反后加 1）
  等价于 (-sum(data)) & 0xFF，也等价于「补上校验字节后整体和 ≡ 0 (mod 256)」

指令一览
--------
  0x70  查询当前浓度（IAQ/TVOC/HCHO/CO/温度/湿度）
  0x72  读取传感器参数（量程 + 单位）
  0x74  读取 LED 状态
  0x56  设置运行灯状态
  0x54  进入休眠模式
  0x55  退出休眠模式
  0x71  获取传感器序列号 SN
  0x73  获取软件版本号
"""

import struct

# ---------------------------------------------------------------- 常量

BAUDRATE = 9600

CMD_CONCENTRATION = 0x70   # 查询当前浓度
CMD_PARAM = 0x72           # 读取传感器参数
CMD_LED_GET = 0x74         # 读取 LED 状态
CMD_LED_SET = 0x56         # 设置运行灯状态
CMD_SLEEP = 0x54           # 进入休眠
CMD_WAKEUP = 0x55          # 退出休眠
CMD_SN = 0x71              # 获取序列号
CMD_VERSION = 0x73         # 获取软件版本号

CMD_NAMES = {
    CMD_CONCENTRATION: "查询当前浓度",
    CMD_PARAM: "读取传感器参数",
    CMD_LED_GET: "读取 LED 状态",
    CMD_LED_SET: "设置运行灯状态",
    CMD_SLEEP: "进入休眠模式",
    CMD_WAKEUP: "退出休眠模式",
    CMD_SN: "获取传感器序列号(SN)",
    CMD_VERSION: "获取软件版本号",
}

# 每条指令期望的应答长度（含最后 1 字节校验）
RX_LEN = {
    CMD_CONCENTRATION: 22,
    CMD_PARAM: 7,
    CMD_LED_GET: 3,
    CMD_LED_SET: 5,
    CMD_SLEEP: 5,
    CMD_WAKEUP: 5,
    CMD_SN: 8,
    CMD_VERSION: 21,
}

# 请求里使用的「气体编号」
GAS_ID_NAME = {0x00: "IAQ", 0x01: "TVOC", 0x02: "HCHO", 0x03: "CO"}

# 应答里回的「气体名称/类型编号」
GAS_TYPE_NAME = {0x33: "IAQ", 0x18: "TVOC", 0x17: "HCHO", 0x19: "CO"}

# 浓度单位编码
UNIT_NAME = {
    0x00: "IAQ（室内空气质量指数，无量纲）",
    0x02: "ppm（百万分比浓度）",
}
UNIT_SHORT = {0x00: "IAQ", 0x02: "ppm"}

# IAQ 等级（EPA 标准）
IAQ_LEVELS = [
    (50, "良好", "#2e9e5b"),
    (100, "中等", "#c9a227"),
    (150, "对敏感人群不健康", "#e07b1a"),
    (200, "不健康", "#d94b2b"),
    (300, "非常不健康", "#8e44ad"),
    (500, "危险", "#7b241c"),
]

# IAQ 等级详细说明（范围, 等级, 建议配色, 含义与建议）
IAQ_GUIDE = [
    (0, 50, "良好", "#2e9e5b",
     "空气质量令人满意，基本无空气污染，各类人群可正常活动。"),
    (51, 100, "中等", "#c9a227",
     "空气质量尚可接受，但某些污染物可能对极少数异常敏感人群健康产生较弱影响，"
     "敏感人群宜减少长时间户外活动。"),
    (101, 150, "对敏感人群不健康", "#e07b1a",
     "儿童、老年人及心脏病、呼吸系统疾病患者应减少长时间户外锻炼。"),
    (151, 200, "不健康", "#d94b2b",
     "所有人群均应减少户外活动，出现明显不适时应及时休息并加强通风净化。"),
    (201, 300, "非常不健康", "#8e44ad",
     "健康人群也会普遍出现明显症状，应避免户外活动，建议开启空气净化。"),
    (301, 500, "危险", "#7b241c",
     "健康警报：所有人应尽量避免户外活动，需采取强制通风或净化措施。"),
    (501, None, "超量程", "#7b241c",
     "超出传感器测量范围，读数仅供参考，请检查环境并通风。"),
]


def iaq_level(v):
    """IAQ 数值 → (等级名, 建议配色)"""
    for limit, name, color in IAQ_LEVELS:
        if v <= limit:
            return name, color
    return "超量程", "#7b241c"


def iaq_guide_index(v):
    """IAQ 数值 → 在 IAQ_GUIDE 中的行号；超出已知范围返回最后一行。"""
    for i, (lo, hi, _n, _c, _a) in enumerate(IAQ_GUIDE):
        if hi is None or v <= hi:
            return i
    return len(IAQ_GUIDE) - 1


# ---------------------------------------------------------------- 基础工具


def add8(data):
    """0-add8 校验和：所有字节求和 → 取低 8 位 → 按位取反 → 加 1。"""
    return (-sum(bytes(data))) & 0xFF


def checksum_ok(frame):
    """校验一帧：带上校验字节后整体和应 ≡ 0 (mod 256)。"""
    if not frame:
        return False
    return add8(frame[:-1]) == frame[-1]


def build(cmd, payload=b""):
    """组帧：指令头 + 数据 + 0-add8 校验和。"""
    body = bytes([cmd]) + bytes(payload)
    return body + bytes([add8(body)])


def hexstr(data):
    """bytes → '70 90' 形式的十六进制字符串（大写）。"""
    return " ".join("%02X" % b for b in bytes(data))


def parse_hex(text):
    """'70 42 30 ...' / '704230...' / '0x70 0x42' → bytes。失败抛 ValueError。"""
    s = "".join(str(text).split()).replace("0x", "").replace("0X", "")
    s = s.replace(",", "")
    if len(s) % 2:
        raise ValueError("十六进制字符数为奇数，可能复制时掉了半个字节")
    return bytes.fromhex(s)


# ---------------------------------------------------------------- 发送帧注释


def describe_tx(tx):
    """把待发送帧逐段翻译成中文说明，返回字符串列表。"""
    tx = bytes(tx)
    if not tx:
        return ["（空帧）"]
    cmd = tx[0]
    lines = ["指令头 0x%02X —— %s" % (cmd, CMD_NAMES.get(cmd, "未知指令"))]

    if cmd == CMD_PARAM and len(tx) >= 2:
        gid = tx[1]
        lines.append("气体编号 0x%02X —— 查询 %s 的量程与单位"
                     % (gid, GAS_ID_NAME.get(gid, "未知通道")))
    elif cmd == CMD_LED_SET and len(tx) >= 2:
        v = tx[1]
        lines.append("LED 控制 0x%02X —— %s（0x00=关闭，0x01=开启）"
                     % (v, "开启运行灯" if v == 0x01 else "关闭运行灯"))
    elif cmd in (CMD_SLEEP, CMD_WAKEUP) and len(tx) >= 2:
        raw = tx[1:-1]
        try:
            s = raw.decode("ascii")
        except UnicodeDecodeError:
            s = hexstr(raw)
        lines.append('命令串 —— ASCII "%s"（%s）'
                     % (s, "让传感器进入低功耗休眠" if cmd == CMD_SLEEP else "唤醒传感器恢复工作"))

    lines.append("校验和 0x%02X —— 0-add8（前 %d 字节求和取低 8 位后取反加 1）"
                 % (tx[-1], len(tx) - 1))
    return lines


# ---------------------------------------------------------------- 应答解析


def _blank(rx):
    return {
        "raw": bytes(rx),
        "hex": hexstr(rx),
        "cmd": None,
        "name": "",
        "checksum_ok": False,
        "length_ok": False,
        "fields": [],          # [(字段名, 原始值, 中文含义)]
        "summary": "",
        "values": None,        # 0x70 时给数值面板用
    }


def decode(rx):
    """
    解析一帧应答。

    返回 dict：
      cmd         指令头
      name        指令中文名
      checksum_ok 校验是否通过
      length_ok   长度是否符合期望
      fields      [(字段名, 原始值, 中文含义), ...]
      summary     一句话中文总结
      values      0x70 时为数值 dict，其余为 None
    """
    rx = bytes(rx)
    out = _blank(rx)

    if not rx:
        out["summary"] = "未收到任何数据（超时）"
        return out

    cmd = rx[0]
    out["cmd"] = cmd
    out["name"] = CMD_NAMES.get(cmd, "未知指令 0x%02X" % cmd)
    out["checksum_ok"] = checksum_ok(rx)

    exp = RX_LEN.get(cmd)
    out["length_ok"] = (exp is None) or (len(rx) == exp)
    if exp and len(rx) != exp:
        out["fields"].append((
            "长度异常",
            "%d 字节" % len(rx),
            "期望 %d 字节，实际不符，后续解析仅供参考" % exp,
        ))

    out["fields"].append((
        "校验和",
        "0x%02X" % rx[-1],
        ("校验通过（0-add8 正确）" if out["checksum_ok"]
         else "校验失败！应为 0x%02X，可能是线路干扰或帧不完整" % add8(rx[:-1])),
    ))

    handler = {
        CMD_CONCENTRATION: _decode_concentration,
        CMD_PARAM: _decode_param,
        CMD_LED_GET: _decode_led_get,
        CMD_LED_SET: _decode_ack,
        CMD_SLEEP: _decode_ack,
        CMD_WAKEUP: _decode_ack,
        CMD_SN: _decode_sn,
        CMD_VERSION: _decode_version,
    }.get(cmd)

    if handler is None:
        out["summary"] = "未知指令头 0x%02X，无法解析" % cmd
        return out

    try:
        handler(rx, out)
    except Exception as e:                                  # 解析异常不让 GUI 崩
        out["fields"].append(("解析异常", "-", str(e)))
        out["summary"] = "数据长度或内容异常，解析失败：%s" % e
    return out


def _decode_concentration(rx, out):
    if len(rx) < 21:
        raise ValueError("浓度帧至少需要 21 字节，实际 %d" % len(rx))
    iaq, tvoc, hcho, co = struct.unpack(">ffff", rx[1:17])
    temp = int.from_bytes(rx[17:19], "big") / 100.0
    hum = int.from_bytes(rx[19:21], "big") / 100.0
    level, _color = iaq_level(iaq)

    out["fields"] = [
        ("IAQ", hexstr(rx[1:5]), "%.2f —— 室内空气质量指数，等级：%s" % (iaq, level)),
        ("TVOC", hexstr(rx[5:9]), "%.4f ppm（总挥发性有机物）" % tvoc),
        ("HCHO", hexstr(rx[9:13]), "%.4f ppm（甲醛）" % hcho),
        ("CO", hexstr(rx[13:17]), "%.4f ppm（一氧化碳）" % co),
        ("温度", hexstr(rx[17:19]), "%.2f ℃（uint16，小数点 2 位）" % temp),
        ("湿度", hexstr(rx[19:21]), "%.2f %%RH（uint16，小数点 2 位）" % hum),
    ] + out["fields"]

    out["summary"] = ("IAQ %.1f（%s）｜TVOC %.3f ppm｜HCHO %.3f ppm｜CO %.3f ppm｜"
                      "温度 %.2f ℃｜湿度 %.2f %%RH"
                      % (iaq, level, tvoc, hcho, co, temp, hum))
    out["values"] = {
        "iaq": iaq, "iaq_level": level, "tvoc": tvoc, "hcho": hcho,
        "co": co, "temp": temp, "hum": hum,
    }


def _decode_param(rx, out):
    if len(rx) < 7:
        raise ValueError("参数帧应为 7 字节，实际 %d" % len(rx))
    gas_id = rx[1]
    gas_type = rx[2]
    rng = int.from_bytes(rx[3:5], "big")
    unit = rx[5]

    out["fields"] = [
        ("气体编号", "0x%02X" % gas_id, GAS_ID_NAME.get(gas_id, "未知通道 0x%02X" % gas_id)),
        ("气体名称", "0x%02X" % gas_type, "传感器类型：%s"
         % GAS_TYPE_NAME.get(gas_type, "未知类型 0x%02X" % gas_type)),
        ("量程", hexstr(rx[3:5]), "0 ~ %d %s（uint16 大端，十进制 %d）"
         % (rng, UNIT_SHORT.get(unit, "?"), rng)),
        ("浓度单位", "0x%02X" % unit, UNIT_NAME.get(unit, "未知单位 0x%02X" % unit)),
    ] + out["fields"]

    out["summary"] = "通道 %s → 气体 %s，量程 0~%d %s" % (
        GAS_ID_NAME.get(gas_id, "0x%02X" % gas_id),
        GAS_TYPE_NAME.get(gas_type, "0x%02X" % gas_type),
        rng, UNIT_SHORT.get(unit, "?"),
    )


def _decode_led_get(rx, out):
    if len(rx) < 3:
        raise ValueError("LED 状态帧应为 3 字节，实际 %d" % len(rx))
    state = rx[1]
    if state == 0x00:
        desc = "0x00 —— 运行灯关闭（常灭）"
    elif state == 0x01:
        desc = "0x01 —— 运行灯开启/闪烁"
    else:
        desc = "0x%02X —— 非 0x00，按文档视为闪烁（开启）" % state
    out["fields"] = [("LED 状态", "0x%02X" % state, desc)] + out["fields"]
    out["summary"] = "运行指示灯：%s" % ("关闭" if state == 0x00 else "开启/闪烁")


def _decode_ack(rx, out):
    """0x54 / 0x55 / 0x56 的通用应答：头 + 保留 + \"OK\" + 校验。"""
    if len(rx) < 5:
        raise ValueError("应答帧应为 5 字节，实际 %d" % len(rx))
    reserved = rx[1]
    ret = rx[2:4]
    try:
        ret_s = ret.decode("ascii")
    except UnicodeDecodeError:
        ret_s = hexstr(ret)
    ok = (ret == b"OK")
    action = {
        CMD_SLEEP: "已进入休眠模式",
        CMD_WAKEUP: "已唤醒，恢复正常工作",
        CMD_LED_SET: "运行灯设置已生效",
    }.get(rx[0], "指令已执行")

    out["fields"] = [
        ("保留字节", "0x%02X" % reserved, "保留位，固定为 0"),
        ("返回值", hexstr(ret) + ' ("%s")' % ret_s,
         "OK —— 执行成功" if ok else "非 OK（0x4F4B 才是 OK），执行可能失败"),
    ] + out["fields"]
    out["summary"] = ("%s（返回值 OK）" % action) if ok else \
                     ("返回值异常 \"%s\"，指令可能未生效" % ret_s)


def _decode_sn(rx, out):
    if len(rx) < 8:
        raise ValueError("序列号帧应为 8 字节，实际 %d" % len(rx))
    sn = rx[1:7]
    sn_hex = hexstr(sn)
    sn_cat = "".join("%02X" % b for b in sn)
    out["fields"] = [
        ("传感器 SN", sn_hex, "十六进制拼接：%s（6 字节唯一序列号）" % sn_cat),
    ] + out["fields"]
    out["summary"] = "传感器序列号 SN = %s" % sn_cat
    out["values"] = {"sn": sn_cat}


def _decode_version(rx, out):
    if len(rx) < 21:
        raise ValueError("版本帧应为 21 字节，实际 %d" % len(rx))
    raw = rx[1:20]
    ver = raw.decode("ascii", errors="replace").rstrip("\x00 ").strip()
    out["fields"] = [
        ("Firmware Version", hexstr(raw), 'ASCII 字符串："%s"（19 字节固件版本）' % ver),
    ] + out["fields"]
    out["summary"] = '固件版本号 = "%s"' % ver
    out["values"] = {"version": ver}


# ---------------------------------------------------------------- 请求构造

def make_request(cmd, payload=b""):
    """构造一条请求，返回 (tx_bytes, 期望应答长度)。"""
    return build(cmd, payload), RX_LEN.get(cmd, 0)


# ---------------------------------------------------------------- 自测

def _self_test():
    """用官方文档里给出的样例帧做一遍回归，确保解析没写错。"""
    cases = [
        # 0x70 查询当前浓度（文档示例）
        ("70 42 30 00 00 3D 35 64 F0 3D 53 D2 5B 3D 08 19 55 0A C8 27 0F E0",
         lambda d: d["checksum_ok"] and abs(d["values"]["iaq"] - 44.0) < 1e-3
         and abs(d["values"]["temp"] - 27.60) < 1e-3
         and abs(d["values"]["hum"] - 99.99) < 1e-3),
        # 0x73 获取软件版本号（文档示例）
        ("73 69 4E 6F 73 65 58 36 32 30 32 35 31 31 31 32 31 34 33 39 A2",
         lambda d: d["checksum_ok"] and d["values"]["version"] == "iNoseX6202511121439"),
        # 0x71 获取序列号（文档示例）
        ("71 12 34 56 78 91 23 C7",
         lambda d: d["checksum_ok"] and d["values"]["sn"] == "123456789123"),
        # 0x74 读取 LED 状态（文档示例）
        ("74 01 8B", lambda d: d["checksum_ok"] and "开启" in d["summary"]),
        # 0x56 设置运行灯（文档示例）
        ("56 00 4F 4B 10", lambda d: d["checksum_ok"] and "OK" in d["summary"]),
        # 0x54 进入休眠（文档示例）
        ("54 00 4F 4B 12", lambda d: d["checksum_ok"]),
        # 0x55 退出休眠（文档示例）
        ("55 00 4F 4B 11", lambda d: d["checksum_ok"]),
    ]
    ok = True
    for h, pred in cases:
        d = decode(parse_hex(h))
        passed = bool(pred(d))
        ok &= passed
        print("%s  %-70s → %s" % ("PASS" if passed else "FAIL", h[:70], d["summary"]))
    print("\n自测结果：%s" % ("全部通过" if ok else "存在失败项"))
    return ok


if __name__ == "__main__":
    import sys
    if len(sys.argv) > 1 and sys.argv[1] == "--hex":
        d = decode(parse_hex(" ".join(sys.argv[2:])))
        print("指令：%s" % d["name"])
        print("HEX ：%s" % d["hex"])
        for label, raw, cn in d["fields"]:
            print("  %-16s %-30s %s" % (label, raw, cn))
        print("总结：%s" % d["summary"])
    else:
        _self_test()

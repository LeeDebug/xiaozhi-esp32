// Pure Modbus RTU protocol helpers. Register addresses are zero based.
export function crc16(bytes) {
  let crc = 0xffff;
  for (const byte of bytes) {
    crc ^= byte;
    for (let bit = 0; bit < 8; bit++) crc = (crc >>> 1) ^ ((crc & 1) ? 0xa001 : 0);
  }
  return crc;
}

export function withCrc(bytes) {
  const crc = crc16(bytes);
  return Uint8Array.from([...bytes, crc & 255, crc >>> 8]);
}

export function hex(bytes) {
  return Array.from(bytes, byte => byte.toString(16).padStart(2, '0').toUpperCase()).join(' ');
}

export function parseHex(text) {
  const compact = text.trim().replace(/\s+/g, '');
  if (!compact || compact.length % 2 || !/^[\da-f]+$/i.test(compact)) {
    throw new Error('请输入完整的十六进制字节，例如 01 03 00 6C 00 01 44 17');
  }
  return Uint8Array.from(compact.match(/../g), byte => Number.parseInt(byte, 16));
}

function validCrc(frame) {
  return frame.length >= 4 && frame.length <= 256 && crc16(frame) === 0;
}

/**
 * Accumulate one RTU interval without interpreting byte patterns. The caller must
 * confirm the frame gap before flush(); only the complete interval is validated.
 * No valid-looking suffix or embedded request is extracted from a rejected frame.
 * Browsers cannot measure wire-level RTU t3.5 precisely; gap timing belongs to
 * the serial adapter/caller, not this byte-only decoder.
 */
export class RtuDecoder {
  constructor(onFrame, onDiscard = () => {}) {
    this.onFrame = onFrame;
    this.onDiscard = onDiscard;
    this.reset();
  }

  reset() { this.buffer = []; this.overflow = false; }

  push(bytes) {
    for (const byte of bytes) {
      if (this.buffer.length < 256) this.buffer.push(byte);
      else this.overflow = true;
    }
  }

  flush() {
    if (!this.buffer.length) return;
    const frame = Uint8Array.from(this.buffer);
    const overflow = this.overflow;
    // Callbacks may push the next interval or flush again. Clear the old state
    // first so neither a valid frame nor a rejected interval can be replayed.
    this.reset();
    let reason = '';
    if (overflow) reason = '帧间隔内数据超过 256 字节，整帧丢弃（仅显示前 256 字节）';
    else if (frame.length < 4) reason = '帧长度不足 4 字节，整帧丢弃';
    else if (frame[0] > 247) reason = '从机地址超出 0..247，整帧丢弃';
    else if ([3, 6].includes(frame[1]) && frame.length !== 8) reason = '03/06 请求必须恰为 8 字节，整帧丢弃';
    else if (!validCrc(frame)) reason = '整帧 CRC 错误，整帧丢弃';
    if (reason) this.onDiscard(frame, reason);
    else this.onFrame(frame);
  }
}

export class ModbusSlave {
  constructor(registers = new Uint16Array(167)) {
    if (!(registers instanceof Uint16Array) || registers.length !== 167) {
      throw new TypeError('寄存器必须为 167 个 Uint16Array 元素');
    }
    this.registers = registers;
    this.address = 1;
  }

  handle(frame) {
    const result = { response: null, summary: '', changed: [], reason: '' };
    const ignore = reason => ({ ...result, summary: reason, reason });
    if (!validCrc(frame)) return ignore('CRC 错误或帧长度无效，未响应');
    const [address, fn] = frame;
    if (address !== this.address && address !== 0) return ignore(`其他从机地址 ${address}，未响应`);
    const broadcast = address === 0;
    const exception = (code, message) => ({
      ...result, response: broadcast ? null : withCrc([address, fn | 0x80, code]),
      summary: message, reason: `异常 0x${code.toString(16).padStart(2, '0')}${broadcast ? '（广播不响应）' : ''}`,
    });
    if (fn !== 3 && fn !== 6) return exception(1, `不支持的功能码 0x${fn.toString(16).padStart(2, '0')}`);
    if (frame.length !== 8) return ignore('03/06 请求长度必须为 8 字节，未响应');
    const start = frame[2] * 256 + frame[3];
    const value = frame[4] * 256 + frame[5];
    const label = `0x${start.toString(16).toUpperCase().padStart(4, '0')} (${start})`;
    if (fn === 3) {
      if (broadcast) return ignore('广播读请求，未响应');
      if (value < 1 || value > 125) return exception(3, `读取数量 ${value} 非法，应为 1..125`);
      if (start + value > this.registers.length) return exception(2, `读取范围 ${label} + ${value} 超出 0..166`);
      const data = [address, fn, value * 2];
      for (const word of this.registers.slice(start, start + value)) data.push(word >>> 8, word & 255);
      return { ...result, response: withCrc(data), summary: `03 读取保持寄存器 ${label}，数量 ${value}` };
    }
    if (start >= this.registers.length) return exception(2, `写入地址 ${label} 超出 0..166`);
    this.registers[start] = value;
    return { ...result, response: broadcast ? null : Uint8Array.from(frame), changed: [start],
      summary: `06 写单寄存器 ${label} = ${value}${broadcast ? '（广播，无响应）' : ''}`,
      reason: broadcast ? '广播写入，不响应' : '' };
  }
}

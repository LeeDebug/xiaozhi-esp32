import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import {
  REGISTER_COUNT, REGISTER_CATALOG, CATALOG_KEYS, BUSINESS_FIELDS, PRESET_VALUES,
  RESERVED_REGISTERS, createPresetRegisters, splitWords, joinWords, writeField, readField,
  describeRegister, formatRegister,
} from './registers.mjs';

// 固件的字段目录是 production_data_report_fields.inc, 本模拟器必须与之保持一致,
// 才能保证「按 docx key 读取」的每一个上报字段都有模拟数据.
const INC = readFileSync(new URL('../production_data_report_fields.inc', import.meta.url), 'utf8');
const firmwareKeys = [...INC.matchAll(/PRODUCTION_REPORT_\w+\("([^"]+)"/g)].map(m => m[1]);
const firmwareFields = [...INC.matchAll(/PRODUCTION_REPORT_(FIELD|BIT_FIELD|FIXED_FIELD)\("([^"]+)",\s*\d+,\s*(0x[0-9A-Fa-f]+),\s*(\d+)?/g)]
  .map(m => ({ kind: m[1], key: m[2], addr: Number.parseInt(m[3], 16), words: m[4] ? Number(m[4]) : 1 }));

test('固件字段目录 152 项全部能在模拟器目录中找到', () => {
  assert.equal(firmwareKeys.length, 152);
  const missing = firmwareKeys.filter(key => !CATALOG_KEYS.includes(key));
  assert.deepEqual(missing, []);
});

test('同名字段的寄存器地址与固件完全一致', () => {
  const byKey = new Map();
  for (const item of REGISTER_CATALOG) if (item.catalog) byKey.set(item.catalog, item);
  const mismatched = firmwareFields
    .filter(f => f.kind !== 'BIT_FIELD')
    .filter(f => byKey.get(f.key)?.addr !== f.addr)
    .map(f => `${f.key}: inc=0x${f.addr.toString(16)} js=${byKey.get(f.key)?.addr}`);
  assert.deepEqual(mismatched, []);
});

test('所有目录项都落在 0..166 且 32 位字段不越界', () => {
  for (const item of REGISTER_CATALOG) {
    assert.ok(item.addr >= 0 && item.addr < REGISTER_COUNT, `${item.name} 地址越界`);
    assert.ok(item.addr + item.words <= REGISTER_COUNT, `${item.name} 字长越界`);
    assert.ok(['R', 'RW', 'W'].includes(item.rw));
  }
});

test('示例数据覆盖全部业务字段, 无报错且只含 uint16', () => {
  const registers = createPresetRegisters();
  assert.equal(registers.length, REGISTER_COUNT);
  assert.ok(registers instanceof Uint16Array);
  for (const field of BUSINESS_FIELDS) {
    const value = readField(registers, field.id);
    assert.equal(value, PRESET_VALUES[field.id], `${field.id} 读回值不一致`);
  }
});

test('预留地址在示例数据里保持 0', () => {
  const registers = createPresetRegisters();
  const covered = new Set(REGISTER_CATALOG.flatMap(item => [item.addr, item.addr + item.words - 1]));
  const highWords = new Set(REGISTER_CATALOG.filter(i => i.words === 2).map(i => i.addr + 1));
  for (const address of RESERVED_REGISTERS) {
    if (covered.has(address) || highWords.has(address)) continue;
    assert.equal(registers[address], 0, `0x${address.toString(16)} 应为 0`);
  }
});

test('报警区 0x4E..0x66 与脱轨/舍内料线报警默认无报警', () => {
  const registers = createPresetRegisters();
  for (let address = 0x4E; address <= 0x66; address++) assert.equal(registers[address], 0);
  assert.equal(registers[0x89], 0);
  assert.equal(registers[0x8A], 0);
  assert.equal(registers[0x8B], 0);
  assert.equal(registers[0x8C], 0);
});

test('单个枚举值都在协议允许范围内', () => {
  const r = createPresetRegisters();
  assert.ok(r[0x6B] <= 2, '主水阀 0/1/2');
  assert.ok(r[0x6E] <= 3, '冲洗 0..3');
  assert.ok(r[0x81] <= 2, '行车模式 0..2');
  for (const address of [0x83, 0x84, 0x85, 0x86]) assert.ok(r[address] <= 3, '料线模式 0..3');
  for (const address of [0x7B, 0x7D, 0x7F]) assert.ok(r[address] <= 7, '行车状态 0..7');
  for (const address of [0x3B, 0x3C, 0x3D, 0x3E]) assert.ok(r[address] <= 100, '亮度 0..100');
  for (let address = 0x8D; address <= 0x94; address++) {
    const hhmm = r[address]; // 时×100 + 分, 例如 0x273B = 10043 -> 23:59
    assert.ok(Math.floor(hhmm / 100) <= 23 && hhmm % 100 <= 59, `0x${address.toString(16)} 时间编码非法`);
  }
  assert.ok(r[0xA6] === 0x03F8);
});

test('32 位字段按低字在前组合, 与固件解码一致', () => {
  const registers = createPresetRegisters();
  // 料塔 1: 12000 kg -> 低字 0x2EE0, 高字 0x0000
  assert.equal(registers[0x02], 12000); assert.equal(registers[0x03], 0);
  assert.equal(joinWords(registers[0x02], registers[0x03], true), 12000);
  const [low, high] = splitWords(0x00050010);
  assert.deepEqual([low, high], [0x0010, 0x0005]);
  assert.equal(joinWords(0x0010, 0x0005), 0x00050010);
  assert.equal(joinWords(0xFFFE, 0xFFFF, true), -2);
});

test('鸡蛋各列计数之和等于总量', () => {
  const registers = createPresetRegisters();
  const total = joinWords(registers[0x96], registers[0x97]);
  let sum = 0;
  for (let i = 0; i < 14; i++) sum += registers[0x98 + i];
  assert.equal(sum, total);
});

test('模拟器目录登记了全部有语义地址, 预留地址之外无遗漏', () => {
  // 32 位字段的高字地址由基地址项隐式覆盖.
  const covered = new Set(REGISTER_CATALOG.flatMap(item => [item.addr, item.addr + item.words - 1]));
  const missing = [];
  for (let address = 0; address < REGISTER_COUNT; address++) {
    if (RESERVED_REGISTERS.includes(address)) continue;
    if (!covered.has(address)) missing.push(`0x${address.toString(16).toUpperCase()}`);
  }
  assert.deepEqual(missing, []);
  // 32 位字段的高字地址也必须被占用
  for (const item of REGISTER_CATALOG.filter(i => i.words === 2)) {
    assert.ok(covered.has(item.addr + 1) || ['s32', 'u32'].includes(item.type), '高字未登记');
  }
});

test('位字段只引用 0xA6, 且与固件 bit 定义一致', () => {
  const bits = REGISTER_CATALOG.filter(item => item.type === 'bit');
  assert.equal(bits.length, 10);
  for (const item of bits) assert.equal(item.addr, 0xA6);
  const incBits = [...INC.matchAll(/BIT_FIELD\("([^"]+)",\s*\d+,\s*0x([0-9A-Fa-f]+),\s*(\d+)\)/g)]
    .map(m => ({ key: m[1], addr: Number.parseInt(m[2], 16), bit: Number(m[3]) }));
  assert.equal(incBits.length, 10);
  for (const inc of incBits) {
    const mine = bits.find(item => item.catalog === inc.key);
    assert.ok(mine, `${inc.key} 缺失`);
    assert.equal(mine.bit, inc.bit);
  }
});

test('名称与格式化输出可用于页面展示', () => {
  const registers = createPresetRegisters();
  assert.match(describeRegister(0x6C), /水温/);
  assert.match(describeRegister(0x02), /1#当前重量/);
  assert.equal(describeRegister(105), '保留 / 未定义');
  assert.equal(formatRegister(0x6C, registers), '25.500');
  assert.equal(formatRegister(0x02, registers), '12000');
  assert.equal(formatRegister(0x4A, registers), '1.850');
  assert.match(formatRegister(0xA6, registers), /水线检测=0/);
  assert.match(formatRegister(0xA6, registers), /称重系统=1/);
});

test('固件按 64 寄存器分块读取时, 模拟器返回的数据能正确解码出全部上报字段', async () => {
  const { ModbusSlave, withCrc } = await import('./protocol.mjs');
  const slave = new ModbusSlave(createPresetRegisters());
  const request = (start, count) => withCrc([1, 3, start >>> 8, start & 255, count >>> 8, count & 255]);
  const snapshot = new Uint16Array(REGISTER_COUNT);
  // 与 production_data_report.cc 的 ReadRegisterChunks 一致: 64 寄存器一块.
  for (let start = 0; start < REGISTER_COUNT; start += 64) {
    const count = Math.min(64, REGISTER_COUNT - start);
    const response = slave.handle(request(start, count)).response;
    // 响应格式: 地址 + 功能码 + 字节数 + 数据 + CRC(2).
    assert.equal(response.length, 3 + count * 2 + 2);
    assert.equal(response[2], count * 2);
    for (let i = 0; i < count; i++) snapshot[start + i] = (response[3 + i * 2] << 8) | response[4 + i * 2];
  }
  assert.deepEqual([...snapshot], [...slave.registers]);

  // 每个业务字段经串口往返后应与预设值一致.
  for (const field of BUSINESS_FIELDS) {
    assert.equal(readField(snapshot, field.id), PRESET_VALUES[field.id], `${field.id} 串口往返不一致`);
  }
  // 抽查固件侧的定点字段解码: 水温 0x6C × 0.001.
  assert.equal(Math.round(snapshot[0x6C] * 0.001 * 1000) / 1000, 25.5);
  assert.equal(snapshot[0x6C], 25500);
});

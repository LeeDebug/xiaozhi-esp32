// 产线设备寄存器目录与示例数据(纯数据 + 纯函数, 不依赖 DOM).
//
// 语义依据(三者一致处优先):
// - 《设备端接入指南》全量字段目录(152 项, mb-mcp_ 键名 -> 保持寄存器地址)
// - 《AI 语音管家与生产监控终端通讯控制逻辑表.xlsx》地址分配表
// - 《AI 语音管家-生产监控终端 PMC Modbus-RTU 完整指令集.md》报文与取值示例
//
// 规则:
// - 地址为零基保持寄存器地址 0..166, 与固件 ProductionModbus 一致.
// - 32 位字段: 低字在前、高字在后; 每个字高字节在前.
// - 定点"浮点"字段是 16 位无符号整数 × 倍率, 不是 IEEE754.
// - 预留/未定义地址(0x03、0x07、0x09、0x0D、...、0x67..0x69、0x97)保持 0;
//   0x0B 是 0x0A(1#加料总累计)的高字, 不是预留地址.

export const REGISTER_COUNT = 167;
export const RESERVED_REGISTERS = [3, 7, 9, 15, 21, 23, 31, 33, 35, 41, 43, 45, 47, 49,
  103, 104, 105, 151];

// 字段类型
const U16 = 'u16';        // 16 位无符号
const S16 = 's16';        // 16 位有符号
const U32 = 'u32';        // 32 位无符号(低字在前)
const S32 = 's32';        // 32 位有符号(低字在前)
const UFIX = 'ufixed';    // 16 位无符号 × scale
const BIT = 'bit';        // 取 1 位

// register: { addr, words, type, scale?, bit?, rw: 'R' | 'RW' | 'W', catalog?, group?, name }
const field = (addr, name, { words = 1, type = U16, scale = 1, bit = null, rw = 'R', catalog = null,
  group = '' } = {}) => ({ addr, name, words, type, scale, bit, rw, catalog, group });

const silo = n => {
  const base = [0x02, 0x0E, 0x1A, 0x26][n - 1];
  const group = `料塔${n}`;
  return [
    field(base, `${n}#当前重量`, { words: 2, type: S32, catalog: `mb-mcp_silo${n}CurrentWeight`, group }),
    field(base + 2, `${n}#实时加料量`, { type: S16, rw: 'RW', catalog: `mb-mcp_silo${n}RealTimeFeedAmount`, group }),
    field(base + 3, `${n}#实时卸料量`, { type: S16, rw: 'RW', catalog: `mb-mcp_silo${n}RealTimeDischargeAmount`, group }),
    field(base + 4, `${n}#本日累计加料`, { words: 2, type: S32, rw: 'RW', catalog: `mb-mcp_silo${n}DailyFeedTotal`, group }),
    field(base + 6, `${n}#本日累计卸料`, { words: 2, type: S32, rw: 'RW', catalog: `mb-mcp_silo${n}DailyDischargeTotal`, group }),
    field(base + 8, `${n}#加料总累计`, { words: 2, type: S32, rw: 'RW', catalog: `mb-mcp_silo${n}TotalFeedAccumulated`, group }),
    field(base + 10, `${n}#卸料总累计`, { words: 2, type: S32, rw: 'RW', catalog: `mb-mcp_silo${n}TotalDischargeAccumulated`, group }),
  ];
};

const waterMeters = ['本日总用水量', '水表1', '水表2', '水表3', '水表4', '水表5', '水表6', '水表7', '水表8']
  .map((name, i) => field(0x32 + i, name, {
    type: UFIX, scale: 0.01,
    catalog: ['mb-mcp_dailyTotalWaterUsage', ...[1, 2, 3, 4, 5, 6, 7, 8].map(n => `mb-mcp_waterMeter${n}`)][i],
    group: '水表',
  }));

const lights = [1, 2, 3, 4].map(n => field(0x3A + n, `当前亮度${n}`, {
  rw: 'RW', catalog: `mb-mcp_currentBrightness${n}`, group: '灯光',
}));

const voltages = [
  ['voltageA1Front', '当前电压值A1（前端操作间）', 0x3F],
  ['voltageB1Front', '当前电压值B1（前端操作间）', 0x40],
  ['voltageC1Front', '当前电压值C1（前端操作间）', 0x41],
  ['voltageA2Rear', '当前电压值A2（后端操作间）', 0x42],
  ['voltageB2Rear', '当前电压值B2（后端操作间）', 0x43],
  ['voltageC2Rear', '当前电压值C2（后端操作间）', 0x44],
  ['energy1Front', '电能1（前端操作间）', 0x45],
  ['energy2Rear', '电能2（后端操作间）', 0x46],
].map(([catalog, name, addr]) => field(addr, name, { catalog: 'mb-mcp_' + catalog, group: '用电量' }));

const alarms = [
  ['waterLineMonitorOfflineAlarm', '水线监测离线报警', 0x4E],
  ['waterLineFlushOfflineAlarm', '水线冲洗离线报警', 0x4F],
  ['drinkingControlOfflineAlarm', '饮水控制离线报警', 0x50],
  ['dimmingModuleOfflineAlarm', '调光模块离线报警', 0x51],
  ['weighingModuleOfflineAlarm', '称重模块离线报警', 0x52],
  ['voltageMonitorOfflineAlarm', '电压监测离线报警', 0x53],
  ...[1, 2, 3, 4, 5, 6, 7, 8].map(n => [`column${n}WaterShortageAlarm`, `${n}列缺水报警`, 0x53 + n]),
  ...[1, 2, 3, 4].map(n => [`lightAlarm${n}`, `灯光报警${n}`, 0x5B + n]),
  ...[1, 2, 3, 4].map(n => [`silo${n}LoadCellFaultAlarm`, `${n}#称重传感器异常报警`, 0x5F + n]),
  ['derailmentDetectionOfflineAlarm', '脱轨检测模块离线报警', 0x64],
  ['feedLineCraneControlOfflineAlarm', '料线行车控制模块离线报警', 0x65],
  ['eggCountingOfflineAlarm', '鸡蛋计数离线报警', 0x66],
].map(([catalog, name, addr]) => field(addr, name, { catalog: 'mb-mcp_' + catalog, group: '报警' }));

const flush = [
  ['dosing', '加药', 0x6D, 'RW'],
  ['flushing', '冲洗', 0x6E, 'RW'],
  ['autoFlushNextRunWeek', '自动冲洗下次运行时间（周）', 0x6F, 'RW'],
  ['autoFlushNextRunHour', '自动冲洗下次运行时间（时）', 0x70, 'RW'],
  ['autoFlushNextRunMinute', '自动冲洗下次运行时间（分）', 0x71, 'RW'],
  ['singleGroupFlushDuration', '单组冲洗时间', 0x72, 'RW'],
  ['concurrentFlushLines', '同时冲洗条数', 0x73, 'RW'],
  ['flushPositionColumn', '冲洗位置(列)', 0x74, 'RW'],
  ...[1, 2, 3, 4, 5, 6].map(n => [`flushPositionLayer${n}`, `冲洗位置${n}(层)`, 0x74 + n, 'RW']),
].map(([catalog, name, addr, rw]) => field(addr, name, { catalog: 'mb-mcp_' + catalog, rw, group: '水线冲洗' }));

const cranes = [
  ['crane3F', '3F行车', 0x7B, 'RW'],
  ['crane3FStartTime', '3F行车启动时间', 0x7C, 'RW'],
  ['crane2F', '2F行车', 0x7D, 'RW'],
  ['crane2FStartTime', '2F行车启动时间', 0x7E, 'RW'],
  ['crane1F', '1F行车', 0x7F, 'RW'],
  ['crane1FStartTime', '1F行车启动时间', 0x80, 'RW'],
  ['craneMode', '行车模式', 0x81, 'R'],
  ['feedProtectionTime', '喂料保护时间', 0x82, 'RW'],
  ...[1, 2, 3, 4].map(n => [`silo${n}FeedLineMode`, `料塔${n}料线模式`, 0x82 + n, 'RW']),
  ['feedLineStatusSilo', '料线状态（料塔）', 0x87, 'RW'],
  ['feedLineStatusHouse', '料线状态（舍内）', 0x88, 'RW'],
  ['houseFeedLineAlarm', '舍内料线报警', 0x89, 'R'],
  ['crane3FDerailment', '3F行车脱轨', 0x8A, 'R'],
  ['crane2FDerailment', '2F行车脱轨', 0x8B, 'R'],
  ['crane1FDerailment', '1F行车脱轨', 0x8C, 'R'],
].map(([catalog, name, addr, rw]) => field(addr, name, { catalog: 'mb-mcp_' + catalog, rw, group: '料线行车' }));

const lightSchedules = [1, 2, 3, 4].flatMap(n => {
  // 0x8D/0x8E 灯光1 开/关, 0x8F/0x90 灯光2 ... 0x93/0x94 灯光4.
  const on = 0x8C + (2 * n - 1);
  return [
    field(on, `当前亮度${n}开灯时间`, { type: S16, rw: 'RW', catalog: `mb-mcp_brightness${n}OnTime`, group: '灯光计划' }),
    field(on + 1, `当前亮度${n}关灯时间`, { type: S16, rw: 'RW', catalog: `mb-mcp_brightness${n}OffTime`, group: '灯光计划' }),
  ];
});

const eggCounts = [
  field(0x96, '鸡蛋计数总量', { words: 2, type: U32, catalog: 'mb-mcp_eggCountTotal', group: '鸡蛋计数' }),
  ...[1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14]
    .map(n => field(0x97 + n, `鸡蛋计数${n}`, { catalog: `mb-mcp_eggCount${n}`, group: '鸡蛋计数' })),
];

const moduleStatus = (() => {
  const bits = [
    ['WaterLineFlush', '水线冲洗'], ['WaterLineDetection', '水线检测'], ['EggCounting', '鸡蛋计数'],
    ['DerailmentDetection', '脱轨检测'], ['FeedLineSystem', '料线系统'], ['VoltageEnergy', '电压电能检测'],
    ['WaterValveControl', '水阀控制'], ['WeighingSystem', '称重系统'],
    ['IotCommunication', '物联网'], ['LightDimming', '灯光调光系统'],
  ];
  return [
    field(0xA6, '模块状态', { catalog: 'mb-mcp_moduleStatus', group: '模块状态' }),
    ...bits.map(([suffix, name], bit) => field(0xA6, `模块状态·位${bit} ${name}`, {
      type: BIT, bit, catalog: `mb-mcp_module${suffix}`, group: '模块状态',
    })),
  ];
})();

/** 全部有语义的寄存器, 按地址升序(同一地址的位字段排在一起). */
export const REGISTER_CATALOG = [
  field(0x00, '版本号', { catalog: 'mb-mcp_version', group: '基础信息' }),
  field(0x01, '通信地址', { rw: 'RW', catalog: 'mb-mcp_commAddress', group: '基础信息' }),
  ...silo(1), ...silo(2), ...silo(3), ...silo(4),
  ...waterMeters, ...lights, ...voltages,
  field(0x47, '鸡只数量', { words: 2, type: U32, catalog: 'mb-mcp_chickenCount', group: '系统状态' }),
  field(0x49, '每日死淘数量', { rw: 'RW', catalog: 'mb-mcp_dailyMortalityCount', group: '系统状态' }),
  field(0x4A, '水料比', { type: UFIX, scale: 0.001, catalog: 'mb-mcp_waterFeedRatio', group: '系统状态' }),
  field(0x4B, '鸡只耗料量', { type: UFIX, scale: 0.001, catalog: 'mb-mcp_chickenFeedConsumption', group: '系统状态' }),
  field(0x4C, '鸡只耗水量', { type: UFIX, scale: 0.001, catalog: 'mb-mcp_chickenWaterConsumption', group: '系统状态' }),
  field(0x4D, '时间(只写)', { rw: 'W', catalog: 'mb-mcp_systemTime', group: '系统状态' }),
  ...alarms,
  field(0x6A, '日志', { catalog: 'mb-mcp_systemLog', group: '系统状态' }),
  field(0x6B, '主水阀', { rw: 'RW', catalog: 'mb-mcp_mainWaterValve', group: '系统状态' }),
  field(0x6C, '水温', { type: UFIX, scale: 0.001, catalog: 'mb-mcp_waterTemperature', group: '系统状态' }),
  ...flush, ...cranes, ...lightSchedules,
  field(0x95, '报警暂停', { rw: 'RW', catalog: 'mb-mcp_alarmPause', group: '报警' }),
  ...eggCounts, ...moduleStatus,
].sort((a, b) => a.addr - b.addr);

/** 按地址升序排列的键列表(含重复地址的位字段). */
export const CATALOG_KEYS = REGISTER_CATALOG.filter(item => item.catalog != null)
  .map(item => item.catalog);

/**
 * 业务字段(可在「模拟设备数据」里按物理量编辑), 与保持寄存器一一对应.
 * view 描述如何在页面展示与解析.
 */
export const BUSINESS_FIELDS = [
  { id: 'version', name: '版本号', addr: 0x00, type: U16, view: 'raw' },
  { id: 'address', name: '通信地址', addr: 0x01, type: U16, view: 'raw' },
  ...[1, 2, 3, 4].map(n => ({ id: `silo${n}`, name: `料塔 ${n} 当前重量 / kg`, addr: [0x02, 0x0E, 0x1A, 0x26][n - 1], type: S32, view: 'int32' })),
  { id: 'waterTotal', name: '本日总用水量 / t', addr: 0x32, type: U16, scale: 0.01, view: 'fixed' },
  ...[1, 2, 3, 4, 5, 6, 7, 8].map(n => ({ id: `water${n}`, name: `水表 ${n} / t`, addr: 0x32 + n, type: U16, scale: 0.01, view: 'fixed' })),
  ...[1, 2, 3, 4].map(n => ({ id: `light${n}`, name: `灯光${n} / %`, addr: 0x3A + n, type: U16, view: 'percent' })),
  { id: 'chickenCount', name: '鸡只数量', addr: 0x47, type: U32, view: 'raw' },
  { id: 'mortality', name: '每日死淘数量', addr: 0x49, type: U16, view: 'raw' },
  { id: 'waterFeedRatio', name: '水料比 / ×0.001', addr: 0x4A, type: U16, scale: 0.001, view: 'fixed' },
  { id: 'feedPerBird', name: '鸡只耗料量 / ×0.001', addr: 0x4B, type: U16, scale: 0.001, view: 'fixed' },
  { id: 'waterPerBird', name: '鸡只耗水量 / ×0.001', addr: 0x4C, type: U16, scale: 0.001, view: 'fixed' },
  { id: 'valve', name: '主水阀', addr: 0x6B, type: U16, view: 'valve' },
  { id: 'temperature', name: '水温 / °C', addr: 0x6C, type: U16, scale: 0.001, view: 'fixed' },
  { id: 'eggTotal', name: '鸡蛋计数总量', addr: 0x96, type: U32, view: 'raw' },
  { id: 'moduleStatus', name: '模块状态 / 位掩码', addr: 0xA6, type: U16, view: 'bits' },
];

// ---------------------------------------------------------------------------
// 示例数据: 一套自洽、无报警的"正常运行"产线快照.
// ---------------------------------------------------------------------------
export const PRESET_VALUES = {
  version: 1106,                  // 指令集实测应答 0x0452 = 1106
  address: 1,
  silo1: 12000, silo2: 8500, silo3: 6400, silo4: 15600,        // kg
  waterTotal: 12.34, water1: 3.21, water2: 2.87, water3: 1.95, water4: 2.44,
  water5: 1.13, water6: 0.74, water7: 0, water8: 0,             // t
  light1: 60, light2: 80, light3: 0, light4: 0,                 // %
  chickenCount: 18000,
  mortality: 12,
  waterFeedRatio: 1.85, feedPerBird: 0.108, waterPerBird: 0.199,
  valve: 2,                       // 2 = 关闭
  temperature: 25.5,              // °C
  eggTotal: 16240,
  moduleStatus: 0x03F8,           // 逻辑表「合同默认配置」: 位3..9 启用, 水线冲洗/检测/鸡蛋计数停用
};

/** 32 位字序号数值 -> 两个寄存器(低字在前). */
export function splitWords(value, signed = false) {
  const v = signed ? BigInt.asUintN(32, BigInt(value)) : BigInt(value);
  return [Number(v & 0xffffn), Number((v >> 16n) & 0xffffn)];
}

/** 两个寄存器(低字在前) -> 32 位数值. */
export function joinWords(low, high, signed = false) {
  const value = (BigInt(high) << 16n) | BigInt(low);
  return signed ? Number(BigInt.asIntN(32, value)) : Number(value);
}

const FIELD_BY_ID = new Map(BUSINESS_FIELDS.map(f => [f.id, f]));

/** 把业务值(物理量)写成寄存器. */
export function writeField(registers, id, value) {
  const f = FIELD_BY_ID.get(id);
  if (!f) throw new Error(`未知字段 ${id}`);
  const scaled = f.scale && f.scale !== 1 ? Math.round(Number(value) / f.scale) : Math.round(Number(value));
  if (f.type === S32 || f.type === U32) {
    const [low, high] = splitWords(scaled, f.type === S32);
    registers[f.addr] = low; registers[f.addr + 1] = high;
  } else {
    registers[f.addr] = ((scaled % 0x10000) + 0x10000) % 0x10000;
  }
  return registers;
}

/** 按字段类型把原始数值写入寄存器, 返回被改动的地址列表. */
export function writeRaw(registers, type, address, value) {
  if (type === S32 || type === U32) {
    const [low, high] = splitWords(value, type === S32);
    registers[address] = low; registers[address + 1] = high;
    return [address, address + 1];
  }
  registers[address] = ((Math.trunc(value) % 0x10000) + 0x10000) % 0x10000;
  return [address];
}

/** 从寄存器读出业务值(物理量); 32 位按低字在前组合. */
export function readField(registers, id) {
  const f = FIELD_BY_ID.get(id);
  if (!f) throw new Error(`未知字段 ${id}`);
  const value = f.type === S32 || f.type === U32
    ? joinWords(registers[f.addr], registers[f.addr + 1], f.type === S32)
    : registers[f.addr];
  return f.scale && f.scale !== 1 ? Math.round(value * f.scale * 1000) / 1000 : value;
}

// 每个业务字段之外的"其余语义寄存器"示例值, 保证 03 读任何区段都有合理数据.
const SKIPPED_BUSINESS = new Set(['version', 'address', 'valve', 'temperature', 'light1', 'light2',
  'light3', 'light4', 'silo1', 'silo2', 'silo3', 'silo4', 'moduleStatus', 'eggTotal',
  'waterTotal', 'water1', 'water2', 'water3', 'water4', 'water5', 'water6', 'water7', 'water8',
  'chickenCount', 'mortality', 'waterFeedRatio', 'feedPerBird', 'waterPerBird']);

function fillRawPreset(registers) {
  const set = (addr, value) => { registers[addr] = value & 0xffff; };
  // 料塔 1..4 的实时/本日/总累计: 塔越大累计越多, 用有小数的比例派生出各自的量.
  const siloRates = [[1200, 260, 8600, 7400, 186000, 172000], [820, 180, 6100, 5200, 132000, 121000],
    [640, 150, 4800, 4100, 98000, 92000], [1500, 340, 10800, 9300, 214000, 205000]];
  [0x02, 0x0E, 0x1A, 0x26].forEach((base, i) => {
    const [feed, discharge, dayFeed, dayDischarge, totalFeed, totalDischarge] = siloRates[i];
    set(base + 2, feed); set(base + 3, discharge);
    const [fl, fh] = splitWords(dayFeed); set(base + 4, fl); set(base + 5, fh);
    const [dl, dh] = splitWords(dayDischarge); set(base + 6, dl); set(base + 7, dh);
    // 加料总累计 = base+8 (2 字), 卸料总累计 = base+10 (2 字).
    const [tl, th] = splitWords(totalFeed); set(base + 8, tl); set(base + 9, th);
    const [ul, uh] = splitWords(totalDischarge); set(base + 10, ul); set(base + 11, uh);
  });
  // 用电量: 电压 217~232V, 电能表读数.
  [0x3F, 0x40, 0x41, 0x42, 0x43, 0x44].forEach((addr, i) => set(addr, [221, 223, 219, 228, 226, 224][i]));
  set(0x45, 4821); set(0x46, 4653);
  // 报警区 0x4E..0x66 全部 0 = 无报警.
  set(0x6A, 0);                       // 日志: 0 无数据
  set(0x6D, 0x0500);                  // 加药: 位8..15 时间参数 5(分钟), 8 路均未运行
  set(0x6E, 0);                       // 冲洗: 0 停止
  set(0x6F, 3);                       // 自动冲洗: 周三
  set(0x70, 6); set(0x71, 30);        // 06:30
  set(0x72, 600);                     // 单组冲洗时间 600
  set(0x73, 3);                       // 同时冲洗条数 3
  set(0x74, 2);                       // 冲洗位置: 第 2 列
  [0x75, 0x76, 0x77, 0x78, 0x79, 0x7A].forEach((addr, i) => set(addr, i < 3 ? i + 1 : 0));
  [0x7B, 0x7D, 0x7F].forEach(addr => { set(addr, 1); set(addr + 1, 8 + (addr === 0x7B ? 0 : 1)); });
  set(0x81, 1);                       // 行车模式: 01 自动
  set(0x82, 45);                      // 喂料保护时间 45
  [0x83, 0x84, 0x85, 0x86].forEach(addr => set(addr, 2)); // 料线模式: 02 自动补料
  set(0x87, 0x0F);                    // 料线状态(料塔): 4 条全部运行
  set(0x88, 0x07);                    // 料线状态(舍内): 3 条全部运行
  set(0x89, 0);                       // 舍内料线报警: 0 正常
  set(0x8A, 0); set(0x8B, 0); set(0x8C, 0); // 行车脱轨: 未报警
  // 灯光计划: 开灯 06:00 / 关灯 18:00, 时×100+分 编码.
  // 编码为 时×100 + 分: 06:00 = 600 = 0x0258, 18:00 = 1800 = 0x0708.
  [0x8D, 0x8F, 0x91, 0x93].forEach(addr => { set(addr, 600); set(addr + 1, 1800); });
  set(0x95, 0);                       // 报警暂停: 未暂停
  // 鸡蛋计数 1..14: 各列计数之和与 0x96 的总量(PRESET_VALUES.eggTotal)严格一致.
  const perColumn = [1210, 1180, 1155, 1220, 1190, 1165, 1205, 1170, 1140, 1188, 1196, 1178, 1145, 0];
  const used = perColumn.reduce((sum, count) => sum + count, 0);
  perColumn[perColumn.length - 1] = PRESET_VALUES.eggTotal - used;
  if (perColumn[perColumn.length - 1] < 0) {
    throw new Error('PRESET_VALUES.eggTotal 小于各列计数之和');
  }
  perColumn.forEach((count, i) => set(0x98 + i, count));
}

/**
 * 生成 167 个寄存器的示例数据.
 * @returns {Uint16Array}
 */
export function createPresetRegisters() {
  const registers = new Uint16Array(REGISTER_COUNT);
  for (const [id, value] of Object.entries(PRESET_VALUES)) writeField(registers, id, value);
  fillRawPreset(registers);
  return registers;
}

/** 按地址取目录项(同地址可能有多个位字段, 返回第一个非位字段或第一个). */
export function describeRegister(address) {
  const items = REGISTER_CATALOG.filter(item => item.addr === address);
  const plain = items.find(item => item.type !== BIT);
  if (plain) return plain.name;
  if (items.length) return items.map(item => item.name.replace(/^模块状态·/, '')).join(' / ');
  return RESERVED_REGISTERS.includes(address) ? '保留 / 未定义' : '未定义';
}

/** 位字段掩码展开, 例如「水线冲洗=0 水线检测=1 ...」. */
export function formatBits(address, registers) {
  const raw = registers[address];
  return REGISTER_CATALOG.filter(item => item.addr === address && item.type === BIT)
    .map(item => `${item.name.replace(/^模块状态·/, '')}=${(raw >> item.bit) & 1}`)
    .join(' ');
}

/** 把原始寄存器格式化为可读值, 供页面表格显示. */
export function formatRegister(address, registers) {
  const items = REGISTER_CATALOG.filter(item => item.addr === address);
  const raw = registers[address];
  const bits = items.filter(item => item.type === BIT);
  const plain = items.find(item => item.type !== BIT);
  if (!plain) {
    if (bits.length) return formatBits(address, registers);
    return '';
  }
  const integer = plain.type === S32 ? `${joinWords(raw, registers[address + 1], true)}`
    : plain.type === U32 ? `${joinWords(raw, registers[address + 1], false)}`
      : plain.type === S16 ? `${(raw << 16) >> 16}`
        : plain.type === UFIX ? `${(Math.round(raw * plain.scale * 1000) / 1000).toFixed(3)}`
          : `${raw}`;
  // 带位字段的整字(0xA6)额外附上位展开, 便于页面直接核对模块启停.
  return bits.length ? `${integer} · ${formatBits(address, registers)}` : integer;
}

/*
 * @file production_data_report_fields.inc
 * @brief 《设备端接入指南》全量字段目录(152 项)的本地映射, 由 production_data_report.cc 展开.
 *
 * 字段顺序与文档附录表格「序号」一致; 省略了表格中的纯保留字地址(如 0x03、0x07)。
 * 每个字段形如 PRODUCTION_REPORT_FIELD(key, 序号, 地址, 字数, 类型)。
 *
 * 类型说明(与文档「说明/取值范围」列对应):
 *   kU16/kS16/kU32/kS32  直接按整数上送;
 *   kUFixed              定点数, 低字 × scale 后按浮点上送;
 *   灯光的「开启/关闭时间」在表格中标注为 16 位无符号数, 但同一份通讯逻辑表规定其编码为
 *   「时×100 + 分」(例如 23:59 对应 27 3B = 0x273B = 10043), 因此按 kS16 原样上送。
 * 位字段 PRODUCTION_REPORT_BIT_FIELD(key, 序号, 地址, 位) 用于 0xA6 模块状态的 10 个位。
 *
 * 本文件只包含数据, 由 .inc 约定不外单独编译。
 */

/* ===== 基础信息 ===== */
PRODUCTION_REPORT_FIELD("mb-mcp_version", 1, 0x00, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_commAddress", 2, 0x01, 1, kU16)

/* ===== 料塔通讯数据(1..4 号塔, 每塔 7 项) ===== */
PRODUCTION_REPORT_FIELD("mb-mcp_silo1CurrentWeight", 3, 0x02, 2, kS32)
PRODUCTION_REPORT_FIELD("mb-mcp_silo1RealTimeFeedAmount", 5, 0x04, 1, kS16)
PRODUCTION_REPORT_FIELD("mb-mcp_silo1RealTimeDischargeAmount", 6, 0x05, 1, kS16)
PRODUCTION_REPORT_FIELD("mb-mcp_silo1DailyFeedTotal", 7, 0x06, 2, kS32)
PRODUCTION_REPORT_FIELD("mb-mcp_silo1DailyDischargeTotal", 9, 0x08, 2, kS32)
PRODUCTION_REPORT_FIELD("mb-mcp_silo1TotalFeedAccumulated", 11, 0x0A, 2, kS32)
PRODUCTION_REPORT_FIELD("mb-mcp_silo1TotalDischargeAccumulated", 13, 0x0C, 2, kS32)

PRODUCTION_REPORT_FIELD("mb-mcp_silo2CurrentWeight", 15, 0x0E, 2, kS32)
PRODUCTION_REPORT_FIELD("mb-mcp_silo2RealTimeFeedAmount", 17, 0x10, 1, kS16)
PRODUCTION_REPORT_FIELD("mb-mcp_silo2RealTimeDischargeAmount", 18, 0x11, 1, kS16)
PRODUCTION_REPORT_FIELD("mb-mcp_silo2DailyFeedTotal", 19, 0x12, 2, kS32)
PRODUCTION_REPORT_FIELD("mb-mcp_silo2DailyDischargeTotal", 21, 0x14, 2, kS32)
PRODUCTION_REPORT_FIELD("mb-mcp_silo2TotalFeedAccumulated", 23, 0x16, 2, kS32)
PRODUCTION_REPORT_FIELD("mb-mcp_silo2TotalDischargeAccumulated", 25, 0x18, 2, kS32)

PRODUCTION_REPORT_FIELD("mb-mcp_silo3CurrentWeight", 27, 0x1A, 2, kS32)
PRODUCTION_REPORT_FIELD("mb-mcp_silo3RealTimeFeedAmount", 29, 0x1C, 1, kS16)
PRODUCTION_REPORT_FIELD("mb-mcp_silo3RealTimeDischargeAmount", 30, 0x1D, 1, kS16)
PRODUCTION_REPORT_FIELD("mb-mcp_silo3DailyFeedTotal", 31, 0x1E, 2, kS32)
PRODUCTION_REPORT_FIELD("mb-mcp_silo3DailyDischargeTotal", 33, 0x20, 2, kS32)
PRODUCTION_REPORT_FIELD("mb-mcp_silo3TotalFeedAccumulated", 35, 0x22, 2, kS32)
PRODUCTION_REPORT_FIELD("mb-mcp_silo3TotalDischargeAccumulated", 37, 0x24, 2, kS32)

PRODUCTION_REPORT_FIELD("mb-mcp_silo4CurrentWeight", 39, 0x26, 2, kS32)
PRODUCTION_REPORT_FIELD("mb-mcp_silo4RealTimeFeedAmount", 41, 0x28, 1, kS16)
PRODUCTION_REPORT_FIELD("mb-mcp_silo4RealTimeDischargeAmount", 42, 0x29, 1, kS16)
PRODUCTION_REPORT_FIELD("mb-mcp_silo4DailyFeedTotal", 43, 0x2A, 2, kS32)
PRODUCTION_REPORT_FIELD("mb-mcp_silo4DailyDischargeTotal", 45, 0x2C, 2, kS32)
PRODUCTION_REPORT_FIELD("mb-mcp_silo4TotalFeedAccumulated", 47, 0x2E, 2, kS32)
PRODUCTION_REPORT_FIELD("mb-mcp_silo4TotalDischargeAccumulated", 49, 0x30, 2, kS32)

/* ===== 水表通讯数据(寄存器 0x32..0x3A, 各 1 字, 单位 0.01t) ===== */
PRODUCTION_REPORT_FIXED_FIELD("mb-mcp_dailyTotalWaterUsage", 51, 0x32, 0.01f)
PRODUCTION_REPORT_FIXED_FIELD("mb-mcp_waterMeter1", 52, 0x33, 0.01f)
PRODUCTION_REPORT_FIXED_FIELD("mb-mcp_waterMeter2", 53, 0x34, 0.01f)
PRODUCTION_REPORT_FIXED_FIELD("mb-mcp_waterMeter3", 54, 0x35, 0.01f)
PRODUCTION_REPORT_FIXED_FIELD("mb-mcp_waterMeter4", 55, 0x36, 0.01f)
PRODUCTION_REPORT_FIXED_FIELD("mb-mcp_waterMeter5", 56, 0x37, 0.01f)
PRODUCTION_REPORT_FIXED_FIELD("mb-mcp_waterMeter6", 57, 0x38, 0.01f)
PRODUCTION_REPORT_FIXED_FIELD("mb-mcp_waterMeter7", 58, 0x39, 0.01f)
PRODUCTION_REPORT_FIXED_FIELD("mb-mcp_waterMeter8", 59, 0x3A, 0.01f)

/* ===== 灯光通讯数据(0x3B..0x3E, 0-100 百分比) ===== */
PRODUCTION_REPORT_FIELD("mb-mcp_currentBrightness1", 60, 0x3B, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_currentBrightness2", 61, 0x3C, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_currentBrightness3", 62, 0x3D, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_currentBrightness4", 63, 0x3E, 1, kU16)

/* ===== 用电量通讯数据 ===== */
PRODUCTION_REPORT_FIELD("mb-mcp_voltageA1Front", 64, 0x3F, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_voltageB1Front", 65, 0x40, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_voltageC1Front", 66, 0x41, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_voltageA2Rear", 67, 0x42, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_voltageB2Rear", 68, 0x43, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_voltageC2Rear", 69, 0x44, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_energy1Front", 70, 0x45, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_energy2Rear", 71, 0x46, 1, kU16)

/* ===== 系统状态(0x47..0x4C) ===== */
PRODUCTION_REPORT_FIELD("mb-mcp_chickenCount", 72, 0x47, 2, kU32)
PRODUCTION_REPORT_FIELD("mb-mcp_dailyMortalityCount", 74, 0x49, 1, kU16)
PRODUCTION_REPORT_FIXED_FIELD("mb-mcp_waterFeedRatio", 75, 0x4A, 0.001f)
PRODUCTION_REPORT_FIXED_FIELD("mb-mcp_chickenFeedConsumption", 76, 0x4B, 0.001f)
PRODUCTION_REPORT_FIXED_FIELD("mb-mcp_chickenWaterConsumption", 77, 0x4C, 0.001f)
/* 0x4D 时间在目录中标注为 W(只写), 默认不会被配置上报; 若服务端配置了该 key, 按 16 位无符号数读取上送。 */
PRODUCTION_REPORT_FIELD("mb-mcp_systemTime", 78, 0x4D, 1, kU16)

/* ===== 报警通讯(0x4E..0x66, 均为 1 字状态码, 按原码上送) ===== */
PRODUCTION_REPORT_FIELD("mb-mcp_waterLineMonitorOfflineAlarm", 79, 0x4E, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_waterLineFlushOfflineAlarm", 80, 0x4F, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_drinkingControlOfflineAlarm", 81, 0x50, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_dimmingModuleOfflineAlarm", 82, 0x51, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_weighingModuleOfflineAlarm", 83, 0x52, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_voltageMonitorOfflineAlarm", 84, 0x53, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_column1WaterShortageAlarm", 85, 0x54, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_column2WaterShortageAlarm", 86, 0x55, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_column3WaterShortageAlarm", 87, 0x56, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_column4WaterShortageAlarm", 88, 0x57, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_column5WaterShortageAlarm", 89, 0x58, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_column6WaterShortageAlarm", 90, 0x59, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_column7WaterShortageAlarm", 91, 0x5A, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_column8WaterShortageAlarm", 92, 0x5B, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_lightAlarm1", 93, 0x5C, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_lightAlarm2", 94, 0x5D, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_lightAlarm3", 95, 0x5E, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_lightAlarm4", 96, 0x5F, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_silo1LoadCellFaultAlarm", 97, 0x60, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_silo2LoadCellFaultAlarm", 98, 0x61, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_silo3LoadCellFaultAlarm", 99, 0x62, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_silo4LoadCellFaultAlarm", 100, 0x63, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_derailmentDetectionOfflineAlarm", 101, 0x64, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_feedLineCraneControlOfflineAlarm", 102, 0x65, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_eggCountingOfflineAlarm", 103, 0x66, 1, kU16)

/* ===== 系统状态(0x6A..0x6C) ===== */
PRODUCTION_REPORT_FIELD("mb-mcp_systemLog", 107, 0x6A, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_mainWaterValve", 108, 0x6B, 1, kU16)
PRODUCTION_REPORT_FIXED_FIELD("mb-mcp_waterTemperature", 109, 0x6C, 0.001f)

/* ===== 水线冲洗的状态和控制(0x6D..0x7A) ===== */
PRODUCTION_REPORT_FIELD("mb-mcp_dosing", 110, 0x6D, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_flushing", 111, 0x6E, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_autoFlushNextRunWeek", 112, 0x6F, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_autoFlushNextRunHour", 113, 0x70, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_autoFlushNextRunMinute", 114, 0x71, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_singleGroupFlushDuration", 115, 0x72, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_concurrentFlushLines", 116, 0x73, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_flushPositionColumn", 117, 0x74, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_flushPositionLayer1", 118, 0x75, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_flushPositionLayer2", 119, 0x76, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_flushPositionLayer3", 120, 0x77, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_flushPositionLayer4", 121, 0x78, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_flushPositionLayer5", 122, 0x79, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_flushPositionLayer6", 123, 0x7A, 1, kU16)

/* ===== 料线行车状态和控制(0x7B..0x8C) ===== */
PRODUCTION_REPORT_FIELD("mb-mcp_crane3F", 124, 0x7B, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_crane3FStartTime", 125, 0x7C, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_crane2F", 126, 0x7D, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_crane2FStartTime", 127, 0x7E, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_crane1F", 128, 0x7F, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_crane1FStartTime", 129, 0x80, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_craneMode", 130, 0x81, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_feedProtectionTime", 131, 0x82, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_silo1FeedLineMode", 132, 0x83, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_silo2FeedLineMode", 133, 0x84, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_silo3FeedLineMode", 134, 0x85, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_silo4FeedLineMode", 135, 0x86, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_feedLineStatusSilo", 136, 0x87, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_feedLineStatusHouse", 137, 0x88, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_houseFeedLineAlarm", 138, 0x89, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_crane3FDerailment", 139, 0x8A, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_crane2FDerailment", 140, 0x8B, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_crane1FDerailment", 141, 0x8C, 1, kU16)

/* ===== 灯光状态和控制(0x8D..0x95) ===== */
/* 「开启/关闭时间」编码为 时×100 + 分(如 23:59 = 0x273B), 按 16 位有符号数上送。 */
PRODUCTION_REPORT_FIELD("mb-mcp_brightness1OnTime", 142, 0x8D, 1, kS16)
PRODUCTION_REPORT_FIELD("mb-mcp_brightness1OffTime", 143, 0x8E, 1, kS16)
PRODUCTION_REPORT_FIELD("mb-mcp_brightness2OnTime", 144, 0x8F, 1, kS16)
PRODUCTION_REPORT_FIELD("mb-mcp_brightness2OffTime", 145, 0x90, 1, kS16)
PRODUCTION_REPORT_FIELD("mb-mcp_brightness3OnTime", 146, 0x91, 1, kS16)
PRODUCTION_REPORT_FIELD("mb-mcp_brightness3OffTime", 147, 0x92, 1, kS16)
PRODUCTION_REPORT_FIELD("mb-mcp_brightness4OnTime", 148, 0x93, 1, kS16)
PRODUCTION_REPORT_FIELD("mb-mcp_brightness4OffTime", 149, 0x94, 1, kS16)
PRODUCTION_REPORT_FIELD("mb-mcp_alarmPause", 150, 0x95, 1, kU16)

/* ===== 鸡蛋计数(0x96..0xA5) ===== */
PRODUCTION_REPORT_FIELD("mb-mcp_eggCountTotal", 151, 0x96, 2, kU32)
PRODUCTION_REPORT_FIELD("mb-mcp_eggCount1", 153, 0x98, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_eggCount2", 154, 0x99, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_eggCount3", 155, 0x9A, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_eggCount4", 156, 0x9B, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_eggCount5", 157, 0x9C, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_eggCount6", 158, 0x9D, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_eggCount7", 159, 0x9E, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_eggCount8", 160, 0x9F, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_eggCount9", 161, 0xA0, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_eggCount10", 162, 0xA1, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_eggCount11", 163, 0xA2, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_eggCount12", 164, 0xA3, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_eggCount13", 165, 0xA4, 1, kU16)
PRODUCTION_REPORT_FIELD("mb-mcp_eggCount14", 166, 0xA5, 1, kU16)

/* ===== 设备停启用状态(0xA6): 整字 + 位0..9 拆位 ===== */
PRODUCTION_REPORT_FIELD("mb-mcp_moduleStatus", 167, 0xA6, 1, kU16)
PRODUCTION_REPORT_BIT_FIELD("mb-mcp_moduleWaterLineFlush", 167, 0xA6, 0)
PRODUCTION_REPORT_BIT_FIELD("mb-mcp_moduleWaterLineDetection", 167, 0xA6, 1)
PRODUCTION_REPORT_BIT_FIELD("mb-mcp_moduleEggCounting", 167, 0xA6, 2)
PRODUCTION_REPORT_BIT_FIELD("mb-mcp_moduleDerailmentDetection", 167, 0xA6, 3)
PRODUCTION_REPORT_BIT_FIELD("mb-mcp_moduleFeedLineSystem", 167, 0xA6, 4)
PRODUCTION_REPORT_BIT_FIELD("mb-mcp_moduleVoltageEnergy", 167, 0xA6, 5)
PRODUCTION_REPORT_BIT_FIELD("mb-mcp_moduleWaterValveControl", 167, 0xA6, 6)
PRODUCTION_REPORT_BIT_FIELD("mb-mcp_moduleWeighingSystem", 167, 0xA6, 7)
PRODUCTION_REPORT_BIT_FIELD("mb-mcp_moduleIotCommunication", 167, 0xA6, 8)
PRODUCTION_REPORT_BIT_FIELD("mb-mcp_moduleLightDimming", 167, 0xA6, 9)

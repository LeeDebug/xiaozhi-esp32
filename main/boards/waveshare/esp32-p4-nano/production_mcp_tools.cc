/*
 * @Date: 2026-09-11 18:52:55
 * @LastEditors: zhouke
 * @LastEditTime: 2026-11-06 15:30:00
 * @FilePath: \xiaozhi-esp32\main\boards\waveshare\esp32-p4-nano\production_mcp_tools.cc
 */
/**
 * @file production_mcp_tools.cc
 * @brief 生产监控终端 MCP 工具集，覆盖 PMC Modbus-RTU 保持寄存器 0x00-0xA6 的
 * 全部可语音化功能：设备信息/地址、料塔称重与加卸料统计、水表、四路灯光与
 * 批量总控/定时、电压电能、系统状态、报警、加药、水线冲洗及计划/参数、
 * 料线行车、料线模式与状态、脱轨、鸡蛋计数、模块启用状态。
 *
 * 数据来源：`docs/big-herdsman-docs/AI 语音管家‑生产监控终端 PMC Modbus‑RTU
 * 完整指令集.md` 与 `docs/big-herdsman-docs/AI语音管家与生产监控终端通讯控制逻辑表.xlsx`。
 *
 * 约定：
 * - 32 位数值低字在前、高字在后；定点数为自定义 2 字节格式（非 IEEE754）。
 * - 位域寄存器一律先读后写（read-modify-write），保留只读位与无关位。
 * - 控制类写操作（冲洗、行车、预约冲洗）先读状态做前置校验，拒绝时抛异常。
 * - 每次调用先校验从机地址，底层失败抛异常，由 MCP 转为工具错误。
 * - 只使用 ProductionModbus 提供的 0x03/0x06；灯光批量总控为 4 次 0x06
 *   顺序写，而非协议文档示例中的单帧 0x10（底层尚未实现 0x10）。
 */
#include "production_mcp_tools.h"

#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>

#include "esp_err.h"
#include "mcp_server.h"
#include "production_modbus.h"

namespace {
// ---------------------------------------------------------------------------
// 寄存器地址（十六进制）。分区与 production_modbus.h 的缓存分区保持一致。
// ---------------------------------------------------------------------------
// 基础设备信息 / 料塔
constexpr uint16_t kVersionRegister = 0x00;
constexpr uint16_t kDeviceAddressRegister = 0x01;
constexpr uint16_t kSiloBlockStart = 0x02;
constexpr uint16_t kSiloBlockSize = 12;  // 每个料塔 12 个寄存器，布局完全相同
constexpr uint16_t kSiloCount = 4;
constexpr uint16_t kSiloBlockCount = kSiloBlockSize * kSiloCount;  // 48
constexpr std::array<uint16_t, 4> kSiloWeightRegisters = {0x02, 0x0E, 0x1A, 0x26};

// 水表 / 灯光 / 电压电能 / 系统状态
constexpr uint16_t kWaterMeterStart = 0x32;
constexpr uint16_t kWaterMeterCount = 9;  // 本日总用水量 + 水表 1-8
constexpr uint16_t kLight1Register = 0x3B;
constexpr uint16_t kLightCount = 4;
constexpr std::array<uint16_t, kLightCount> kLightRegisters = {0x3B, 0x3C, 0x3D, 0x3E};
constexpr uint16_t kMinimumBrightness = 0;
constexpr uint16_t kMaximumBrightness = 100;
constexpr uint16_t kPowerStart = 0x3F;
constexpr uint16_t kPowerCount = 8;  // 6 路电压 + 2 路电能
constexpr uint16_t kSystemStatusStart = 0x47;
constexpr uint16_t kSystemStatusCount = 7;  // 0x47-0x4D
constexpr uint16_t kDailyMortalityRegister = 0x49;
constexpr uint16_t kChickenCountRegister = 0x47;

// 报警区（与 production_modbus.h 的 kAlarmStart/kAlarmCount 对齐）
constexpr uint16_t kAlarmStart = 0x4E;
constexpr uint16_t kAlarmCount = 25;  // 0x4E-0x66

// 水阀 / 水温 / 加药 / 冲洗
constexpr uint16_t kMainWaterValveRegister = 0x6B;
constexpr uint16_t kWaterTemperatureRegister = 0x6C;
constexpr uint16_t kMainWaterValveOpenValue = 1;
constexpr uint16_t kMainWaterValveClosedValue = 2;
constexpr uint16_t kDeviceLogRegister = 0x6A;
constexpr uint16_t kDosingRegister = 0x6D;
constexpr uint16_t kFlushStateRegister = 0x6E;
constexpr uint16_t kFlushBlockStart = 0x6E;
constexpr uint16_t kFlushBlockCount = 13;  // 0x6E-0x7A
constexpr uint16_t kFlushScheduleWeekRegister = 0x6F;
constexpr uint16_t kFlushScheduleHourRegister = 0x70;
constexpr uint16_t kFlushScheduleMinuteRegister = 0x71;
constexpr uint16_t kFlushGroupDurationRegister = 0x72;
constexpr uint16_t kFlushParallelLinesRegister = 0x73;
constexpr uint16_t kFlushColumnRegister = 0x74;
constexpr uint16_t kFlushLayerStart = 0x75;  // 0x75-0x7A 共 6 层

// 行车 / 料线 / 灯光定时 / 报警暂停 / 鸡蛋 / 模块
constexpr uint16_t kCartBlockStart = 0x7B;
constexpr uint16_t kCartBlockCount = 8;  // 0x7B-0x82
constexpr uint16_t kCartModeRegister = 0x81;
constexpr uint16_t kFeedProtectionRegister = 0x82;
constexpr std::array<uint16_t, 3> kCartStateRegisters = {0x7F, 0x7D, 0x7B};  // 1F/2F/3F
constexpr std::array<uint16_t, 3> kCartStartTimeRegisters = {0x80, 0x7E, 0x7C};
constexpr uint16_t kFeedLineBlockStart = 0x83;
constexpr uint16_t kFeedLineBlockCount = 7;  // 0x83-0x89
constexpr uint16_t kFeedLineModeStart = 0x83;
constexpr uint16_t kFeedLineSiloStatusRegister = 0x87;
constexpr uint16_t kFeedLineHouseStatusRegister = 0x88;
constexpr uint16_t kDerailmentStart = 0x8A;
constexpr uint16_t kDerailmentCount = 3;
constexpr uint16_t kLightScheduleStart = 0x8D;
constexpr uint16_t kLightScheduleCount = 8;
constexpr uint16_t kAlarmPauseRegister = 0x95;
constexpr uint16_t kEggStart = 0x96;
constexpr uint16_t kEggCount = 16;  // 总量 2 字 + 14 列
constexpr uint16_t kModuleStatusRegister = 0xA6;

// 换算与取值边界
constexpr double kWaterTemperatureScale = 0.001;  // 水温 0.000-99.999
constexpr double kFixed1000Scale = 0.001;         // 水料比 / 耗料 / 耗水
constexpr double kFixed100Scale = 0.01;           // 水表 0.00-655.35t
constexpr uint16_t kMaximumDeviceAddress = 127;   // 协议 1-127，非 Modbus 通用 247
constexpr uint16_t kMaximumDosingMinutes = 100;
constexpr uint16_t kMultipleAlarmCode = 0x1E;

// ---------------------------------------------------------------------------
// 通用辅助
// ---------------------------------------------------------------------------
void EnsureDeviceAddressIsConfigured(const ProductionModbus& modbus) {
    if (modbus.GetDeviceAddress() == ProductionModbus::kUnsetDeviceAddress) {
        throw std::runtime_error("Modbus device address is not configured");
    }
}

std::runtime_error MakeModbusError(const char* operation, esp_err_t err) {
    return std::runtime_error(std::string(operation) + ": " + esp_err_to_name(err));
}

uint16_t ReadRegisterOrThrow(ProductionModbus& modbus, uint16_t register_address,
                             const char* operation) {
    uint16_t value = 0;
    esp_err_t err = modbus.ReadHoldingRegister(register_address, value);
    if (err != ESP_OK) {
        throw MakeModbusError(operation, err);
    }
    return value;
}

void WriteRegisterOrThrow(ProductionModbus& modbus, uint16_t register_address, uint16_t value,
                          const char* operation) {
    esp_err_t err = modbus.WriteHoldingRegister(register_address, value);
    if (err != ESP_OK) {
        throw MakeModbusError(operation, err);
    }
}

// 一次功能码 0x03 读取一个连续寄存器块，减少 RS485 往返。
template <std::size_t N>
std::array<uint16_t, N> ReadBlockOrThrow(ProductionModbus& modbus, uint16_t start,
                                         const char* operation) {
    std::array<uint16_t, N> values{};
    esp_err_t err = modbus.ReadHoldingRegisters(start, static_cast<uint16_t>(N), values.data());
    if (err != ESP_OK) {
        throw MakeModbusError(operation, err);
    }
    return values;
}

cJSON* CreateSuccessResult() {
    cJSON* result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "success", true);
    return result;
}

template <std::size_t N>
void AddRawValues(cJSON* result, const std::array<uint16_t, N>& values, uint16_t start) {
    cJSON_AddNumberToObject(result, "start_register", start);
    cJSON_AddNumberToObject(result, "register_count", N);
    cJSON* raw = cJSON_AddArrayToObject(result, "raw_values");
    for (uint16_t value : values) {
        cJSON_AddItemToArray(raw, cJSON_CreateNumber(value));
    }
}

// 32 位数值：先返回的寄存器为低 16 位。
int32_t CombineSigned32(const uint16_t* words) {
    uint32_t raw = (static_cast<uint32_t>(words[1]) << 16) | words[0];
    return static_cast<int32_t>(raw);
}

uint32_t CombineUnsigned32(const uint16_t* words) {
    return (static_cast<uint32_t>(words[1]) << 16) | words[0];
}

// 生成位域对象。high_bit_first=true 时 names[0] 对应最高位（协议表
// 中「从高到低分别表示…」的表述）；否则 names[0] 对应 bit0。
cJSON* CreateBitField(uint16_t value, const char* const* names, std::size_t count,
                      bool high_bit_first) {
    cJSON* field = cJSON_CreateObject();
    for (std::size_t i = 0; i < count; ++i) {
        std::size_t bit = high_bit_first ? (count - 1 - i) : i;
        cJSON_AddBoolToObject(field, names[i], ((value >> bit) & 1U) != 0);
    }
    cJSON_AddNumberToObject(field, "raw_value", value);
    return field;
}

// 保留高字节、只替换低 8 位（行车/冲洗寄存器的 bit[8:15] 为只读）。
uint16_t ReplaceLowByte(uint16_t previous, uint8_t low_byte) {
    return static_cast<uint16_t>((previous & 0xFF00) | low_byte);
}

// ---------------------------------------------------------------------------
// 状态名与位名
// ---------------------------------------------------------------------------
const char* WaterValveStateName(uint16_t value) {
    switch (value) {
        case 0:
            return "none";
        case kMainWaterValveOpenValue:
            return "open";
        case kMainWaterValveClosedValue:
            return "closed";
        default:
            return "unknown";
    }
}

const char* FlushStateName(uint16_t state) {
    switch (state) {
        case 0:
            return "stopped";
        case 1:
            return "flushing";
        case 2:
            return "auto_refill";
        case 3:
            return "high_temperature_water_change";
        default:
            return "unknown";
    }
}

const char* CartStateName(uint16_t state) {
    static constexpr const char* kNames[] = {"none",
                                             "front_end_reached",
                                             "rear_end_reached",
                                             "moving_forward",
                                             "returning",
                                             "paused",
                                             "fault",
                                             "running_without_full_feed"};
    return state < 8 ? kNames[state] : "unknown";
}

const char* CartModeName(uint16_t mode) {
    switch (mode) {
        case 0:
            return "emergency";
        case 1:
            return "automatic";
        case 2:
            return "remote";
        default:
            return "unknown";
    }
}

const char* FeedLineModeName(uint16_t mode) {
    switch (mode) {
        case 0:
            return "none";
        case 1:
            return "timed_refill";
        case 2:
            return "automatic_refill";
        case 3:
            return "paused_refill";
        default:
            return "unknown";
    }
}

cJSON* CreateLightStatus(uint8_t channel, uint16_t brightness) {
    cJSON* result = cJSON_CreateObject();
    cJSON_AddNumberToObject(result, "channel", channel);
    cJSON_AddBoolToObject(result, "on", brightness != 0);
    cJSON_AddNumberToObject(result, "brightness_percent", brightness);
    return result;
}

uint16_t SetLightBrightness(ProductionModbus& modbus, uint8_t channel, uint16_t brightness) {
    WriteRegisterOrThrow(modbus, kLightRegisters[channel - 1], brightness,
                         "Failed to set light brightness");
    return brightness;
}

int32_t ReadSiloWeight(ProductionModbus& modbus, uint16_t start_register, uint16_t* words) {
    esp_err_t err = modbus.ReadHoldingRegisters(start_register, 2, words);
    if (err != ESP_OK) {
        throw MakeModbusError("Failed to read silo weight", err);
    }
    return CombineSigned32(words);
}

// ---------------------------------------------------------------------------
// 报警区定义（按 0x4E 起的寄存器顺序，共 25 项）
// ---------------------------------------------------------------------------
enum class AlarmKind { kModule, kLine, kLight, kBoolean };
struct AlarmDefinition {
    const char* key;
    AlarmKind kind;
    uint16_t units;  // kModule/kLine 时表示最大模块/水线编号
};

constexpr AlarmDefinition kAlarmDefinitions[kAlarmCount] = {
    {"water_line_monitor_offline", AlarmKind::kModule, 28},    // 0x4E
    {"water_line_flush_offline", AlarmKind::kModule, 22},      // 0x4F
    {"drinking_control_offline", AlarmKind::kModule, 8},       // 0x50
    {"dimmer_module_offline", AlarmKind::kModule, 4},          // 0x51
    {"weighing_module_offline", AlarmKind::kModule, 2},        // 0x52
    {"voltage_monitor_offline", AlarmKind::kModule, 2},        // 0x53
    {"column_1_water_shortage", AlarmKind::kLine, 28},         // 0x54
    {"column_2_water_shortage", AlarmKind::kLine, 28},         // 0x55
    {"column_3_water_shortage", AlarmKind::kLine, 28},         // 0x56
    {"column_4_water_shortage", AlarmKind::kLine, 28},         // 0x57
    {"column_5_water_shortage", AlarmKind::kLine, 28},         // 0x58
    {"column_6_water_shortage", AlarmKind::kLine, 28},         // 0x59
    {"column_7_water_shortage", AlarmKind::kLine, 28},         // 0x5A
    {"column_8_water_shortage", AlarmKind::kLine, 28},         // 0x5B
    {"light_1_alarm", AlarmKind::kLight, 0},                   // 0x5C
    {"light_2_alarm", AlarmKind::kLight, 0},                   // 0x5D
    {"light_3_alarm", AlarmKind::kLight, 0},                   // 0x5E
    {"light_4_alarm", AlarmKind::kLight, 0},                   // 0x5F
    {"silo_1_load_cell_alarm", AlarmKind::kBoolean, 0},        // 0x60
    {"silo_2_load_cell_alarm", AlarmKind::kBoolean, 0},        // 0x61
    {"silo_3_load_cell_alarm", AlarmKind::kBoolean, 0},        // 0x62
    {"silo_4_load_cell_alarm", AlarmKind::kBoolean, 0},        // 0x63
    {"derailment_detector_offline", AlarmKind::kModule, 7},    // 0x64
    {"feed_cart_controller_offline", AlarmKind::kBoolean, 0},  // 0x65
    {"egg_counter_offline", AlarmKind::kModule, 14},           // 0x66
};

std::string DescribeAlarm(uint16_t value, const AlarmDefinition& definition) {
    if (value == 0) {
        return "none";
    }
    if (definition.kind == AlarmKind::kLight) {
        if (value == kMultipleAlarmCode) {
            return "overload_and_overheat";
        }
        if (value == 1) {
            return "overload";
        }
        if (value == 2) {
            return "overheat";
        }
        return "unknown";
    }
    if (definition.kind == AlarmKind::kBoolean) {
        return value == 1 ? "alarm" : "unknown";
    }
    if (value == kMultipleAlarmCode) {
        return "multiple";
    }
    if (value <= definition.units) {
        return "unit_" + std::to_string(value);
    }
    return "unknown";
}

constexpr const char* const kModuleStatusNames[16] = {
    "water_line_flush",     "water_line_monitor", "egg_counter",
    "derailment_detection", "feed_line_system",   "power_monitor",
    "water_valve",          "weighing_system",    "iot",
    "light_dimmer",         "reserved_10",        "reserved_11",
    "reserved_12",          "reserved_13",        "reserved_14",
    "reserved_15"};

constexpr const char* const kDosingChannelNames[8] = {
    "dosing_1", "dosing_2", "dosing_3", "dosing_4", "dosing_5", "dosing_6", "dosing_7", "dosing_8"};

constexpr const char* const kSiloFeedLineNames[4] = {"line_1", "line_2", "line_3", "line_4"};
constexpr const char* const kHouseFeedLineNames[3] = {"line_1", "line_2", "line_3"};
constexpr const char* const kDerailmentCartNames[7] = {"cart_1", "cart_2", "cart_3", "cart_4",
                                                       "cart_5", "cart_6", "cart_7"};

// 时间编码：高字节小时、低字节分钟（协议表示例 23:59）。
bool ParseClockValue(const std::string& text, uint16_t& encoded) {
    if (text.size() != 5 || text[2] != ':') {
        return false;
    }
    for (int i = 0; i < 5; ++i) {
        if (i == 2) {
            continue;
        }
        if (!std::isdigit(static_cast<unsigned char>(text[i]))) {
            return false;
        }
    }
    int hour = (text[0] - '0') * 10 + (text[1] - '0');
    int minute = (text[3] - '0') * 10 + (text[4] - '0');
    if (hour > 23 || minute > 59) {
        return false;
    }
    encoded = static_cast<uint16_t>((hour << 8) | minute);
    return true;
}

std::string FormatClockValue(uint16_t raw) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%02u:%02u", static_cast<unsigned>((raw >> 8) & 0xFF),
                  static_cast<unsigned>(raw & 0xFF));
    return std::string(buffer);
}

// 冲洗参数名到寄存器/取值范围的映射。
struct FlushConfigDefinition {
    const char* name;
    uint16_t register_address;
    uint16_t minimum;
    uint16_t maximum;
};
constexpr FlushConfigDefinition kFlushConfigDefinitions[9] = {
    {"group_duration", kFlushGroupDurationRegister, 0, 9999},
    {"parallel_lines", kFlushParallelLinesRegister, 1, 7},
    {"column", kFlushColumnRegister, 1, 7},
    {"layer_1", kFlushLayerStart, 1, 0xA8},
    {"layer_2", kFlushLayerStart + 1, 1, 0xA8},
    {"layer_3", kFlushLayerStart + 2, 1, 0xA8},
    {"layer_4", kFlushLayerStart + 3, 1, 0xA8},
    {"layer_5", kFlushLayerStart + 4, 1, 0xA8},
    {"layer_6", kFlushLayerStart + 5, 1, 0xA8},
};

uint16_t CartStateRegister(int floor) { return kCartStateRegisters[floor - 1]; }
uint16_t CartStartTimeRegister(int floor) { return kCartStartTimeRegisters[floor - 1]; }
}  // namespace

void InitializeProductionMcpTools() {
    static bool initialized = false;
    if (initialized) {
        return;
    }
    initialized = true;

    auto& server = McpServer::GetInstance();

    // =======================================================================
    // 已有工具：主水阀 / 水温 / 灯光（扩展至 4 路）/ 料塔当前重量
    // =======================================================================
    server.AddTool(
        "self.production.main_water_valve.set",
        "打开或关闭主水阀。操作保持寄存器 0x6B，写入 1 打开、写入 2 关闭。",
        PropertyList({Property("open", kPropertyTypeBoolean)}),
        [](const PropertyList& properties) -> ReturnValue {
            auto& modbus = ProductionModbus::GetInstance();
            EnsureDeviceAddressIsConfigured(modbus);

            bool open = properties["open"].value<bool>();
            uint16_t requested_value = open ? kMainWaterValveOpenValue : kMainWaterValveClosedValue;
            uint16_t previous_value = ReadRegisterOrThrow(modbus, kMainWaterValveRegister,
                                                          "Failed to read main water valve state");
            WriteRegisterOrThrow(modbus, kMainWaterValveRegister, requested_value,
                                 "Failed to set main water valve");

            cJSON* result = cJSON_CreateObject();
            cJSON_AddBoolToObject(result, "success", true);
            cJSON_AddStringToObject(result, "state", WaterValveStateName(requested_value));
            cJSON_AddStringToObject(result, "previous_state", WaterValveStateName(previous_value));
            cJSON_AddNumberToObject(result, "register", kMainWaterValveRegister);
            cJSON_AddNumberToObject(result, "raw_value", requested_value);
            cJSON_AddNumberToObject(result, "previous_raw_value", previous_value);
            return result;
        });

    server.AddTool("self.production.main_water_valve.get",
                   "读取保持寄存器 0x6B，查看主水阀状态：0 无状态、1 打开、2 关闭。",
                   PropertyList(), [](const PropertyList&) -> ReturnValue {
                       auto& modbus = ProductionModbus::GetInstance();
                       EnsureDeviceAddressIsConfigured(modbus);
                       uint16_t value =
                           ReadRegisterOrThrow(modbus, kMainWaterValveRegister,
                                               "Failed to read main water valve state");

                       cJSON* result = cJSON_CreateObject();
                       cJSON_AddBoolToObject(result, "success", true);
                       cJSON_AddStringToObject(result, "state", WaterValveStateName(value));
                       cJSON_AddNumberToObject(result, "register", kMainWaterValveRegister);
                       cJSON_AddNumberToObject(result, "raw_value", value);
                       return result;
                   });

    server.AddTool("self.production.water_temperature.get",
                   "按需读取保持寄存器 0x6C 中的水温。协议值按 0.001 摄氏度换算。", PropertyList(),
                   [](const PropertyList&) -> ReturnValue {
                       auto& modbus = ProductionModbus::GetInstance();
                       EnsureDeviceAddressIsConfigured(modbus);
                       uint16_t raw_value = ReadRegisterOrThrow(modbus, kWaterTemperatureRegister,
                                                                "Failed to read water temperature");

                       cJSON* result = cJSON_CreateObject();
                       cJSON_AddBoolToObject(result, "success", true);
                       cJSON_AddNumberToObject(result, "temperature_celsius",
                                               raw_value * kWaterTemperatureScale);
                       cJSON_AddNumberToObject(result, "register", kWaterTemperatureRegister);
                       cJSON_AddNumberToObject(result, "raw_value", raw_value);
                       return result;
                   });

    server.AddTool("self.production.light.get",
                   "查看指定一路灯光状态和当前亮度百分比。channel 取值为 1-4。",
                   PropertyList({Property("channel", kPropertyTypeInteger, 1, kLightCount)}),
                   [](const PropertyList& properties) -> ReturnValue {
                       auto& modbus = ProductionModbus::GetInstance();
                       EnsureDeviceAddressIsConfigured(modbus);
                       int channel = properties["channel"].value<int>();
                       uint16_t brightness = ReadRegisterOrThrow(
                           modbus, kLightRegisters[channel - 1], "Failed to read light state");

                       cJSON* result = CreateLightStatus(static_cast<uint8_t>(channel), brightness);
                       cJSON_AddBoolToObject(result, "success", true);
                       return result;
                   });

    server.AddTool("self.production.lights.get", "一次读取并返回灯光 1-4 的开关状态及亮度百分比。",
                   PropertyList(), [](const PropertyList&) -> ReturnValue {
                       auto& modbus = ProductionModbus::GetInstance();
                       EnsureDeviceAddressIsConfigured(modbus);
                       auto brightness = ReadBlockOrThrow<kLightCount>(
                           modbus, kLight1Register, "Failed to read all light states");

                       cJSON* result = CreateSuccessResult();
                       cJSON* lights = cJSON_AddArrayToObject(result, "lights");
                       for (std::size_t i = 0; i < kLightCount; ++i) {
                           cJSON_AddItemToArray(
                               lights,
                               CreateLightStatus(static_cast<uint8_t>(i + 1), brightness[i]));
                       }
                       AddRawValues(result, brightness, kLight1Register);
                       return result;
                   });

    server.AddTool("self.production.light_1.open",
                   "打开灯光一并设置亮度百分比。brightness_percent 取值 0-100，0 等同关闭。",
                   PropertyList({Property("brightness_percent", kPropertyTypeInteger,
                                          kMinimumBrightness, kMaximumBrightness)}),
                   [](const PropertyList& properties) -> ReturnValue {
                       auto& modbus = ProductionModbus::GetInstance();
                       EnsureDeviceAddressIsConfigured(modbus);
                       uint16_t brightness =
                           static_cast<uint16_t>(properties["brightness_percent"].value<int>());
                       uint16_t actual_value = SetLightBrightness(modbus, 1, brightness);
                       cJSON* result = CreateLightStatus(1, actual_value);
                       cJSON_AddBoolToObject(result, "success", true);
                       return result;
                   });

    server.AddTool("self.production.light_1.close", "关闭灯光一，将保持寄存器 0x3B 写为 0。",
                   PropertyList(), [](const PropertyList&) -> ReturnValue {
                       auto& modbus = ProductionModbus::GetInstance();
                       EnsureDeviceAddressIsConfigured(modbus);
                       uint16_t actual_value = SetLightBrightness(modbus, 1, 0);
                       cJSON* result = CreateLightStatus(1, actual_value);
                       cJSON_AddBoolToObject(result, "success", true);
                       return result;
                   });

    server.AddTool("self.production.light_2.open",
                   "打开灯光二并设置亮度百分比。brightness_percent 取值 0-100，0 等同关闭。",
                   PropertyList({Property("brightness_percent", kPropertyTypeInteger,
                                          kMinimumBrightness, kMaximumBrightness)}),
                   [](const PropertyList& properties) -> ReturnValue {
                       auto& modbus = ProductionModbus::GetInstance();
                       EnsureDeviceAddressIsConfigured(modbus);
                       uint16_t brightness =
                           static_cast<uint16_t>(properties["brightness_percent"].value<int>());
                       uint16_t actual_value = SetLightBrightness(modbus, 2, brightness);
                       cJSON* result = CreateLightStatus(2, actual_value);
                       cJSON_AddBoolToObject(result, "success", true);
                       return result;
                   });

    server.AddTool("self.production.light_2.close", "关闭灯光二，将保持寄存器 0x3C 写为 0。",
                   PropertyList(), [](const PropertyList&) -> ReturnValue {
                       auto& modbus = ProductionModbus::GetInstance();
                       EnsureDeviceAddressIsConfigured(modbus);
                       uint16_t actual_value = SetLightBrightness(modbus, 2, 0);
                       cJSON* result = CreateLightStatus(2, actual_value);
                       cJSON_AddBoolToObject(result, "success", true);
                       return result;
                   });

    server.AddTool("self.production.light_3.open",
                   "打开灯光三并设置亮度百分比，写保持寄存器 0x3D。brightness_percent 取值 0-100。",
                   PropertyList({Property("brightness_percent", kPropertyTypeInteger,
                                          kMinimumBrightness, kMaximumBrightness)}),
                   [](const PropertyList& properties) -> ReturnValue {
                       auto& modbus = ProductionModbus::GetInstance();
                       EnsureDeviceAddressIsConfigured(modbus);
                       uint16_t brightness =
                           static_cast<uint16_t>(properties["brightness_percent"].value<int>());
                       uint16_t actual_value = SetLightBrightness(modbus, 3, brightness);
                       cJSON* result = CreateLightStatus(3, actual_value);
                       cJSON_AddBoolToObject(result, "success", true);
                       return result;
                   });

    server.AddTool("self.production.light_3.close", "关闭灯光三，将保持寄存器 0x3D 写为 0。",
                   PropertyList(), [](const PropertyList&) -> ReturnValue {
                       auto& modbus = ProductionModbus::GetInstance();
                       EnsureDeviceAddressIsConfigured(modbus);
                       uint16_t actual_value = SetLightBrightness(modbus, 3, 0);
                       cJSON* result = CreateLightStatus(3, actual_value);
                       cJSON_AddBoolToObject(result, "success", true);
                       return result;
                   });

    server.AddTool("self.production.light_4.open",
                   "打开灯光四并设置亮度百分比，写保持寄存器 0x3E。brightness_percent 取值 0-100。",
                   PropertyList({Property("brightness_percent", kPropertyTypeInteger,
                                          kMinimumBrightness, kMaximumBrightness)}),
                   [](const PropertyList& properties) -> ReturnValue {
                       auto& modbus = ProductionModbus::GetInstance();
                       EnsureDeviceAddressIsConfigured(modbus);
                       uint16_t brightness =
                           static_cast<uint16_t>(properties["brightness_percent"].value<int>());
                       uint16_t actual_value = SetLightBrightness(modbus, 4, brightness);
                       cJSON* result = CreateLightStatus(4, actual_value);
                       cJSON_AddBoolToObject(result, "success", true);
                       return result;
                   });

    server.AddTool("self.production.light_4.close", "关闭灯光四，将保持寄存器 0x3E 写为 0。",
                   PropertyList(), [](const PropertyList&) -> ReturnValue {
                       auto& modbus = ProductionModbus::GetInstance();
                       EnsureDeviceAddressIsConfigured(modbus);
                       uint16_t actual_value = SetLightBrightness(modbus, 4, 0);
                       cJSON* result = CreateLightStatus(4, actual_value);
                       cJSON_AddBoolToObject(result, "success", true);
                       return result;
                   });

    server.AddTool("self.production.lights.set_all",
                   "一次性设置全部 4 路灯光亮度（0-100，0 为关灯）。底层仅支持功能码 0x06，"
                   "因此按 0x3B-0x3E 顺序写 4 个寄存器并回读确认，不是单帧 0x10 原子写。",
                   PropertyList({Property("brightness_percent", kPropertyTypeInteger,
                                          kMinimumBrightness, kMaximumBrightness)}),
                   [](const PropertyList& properties) -> ReturnValue {
                       auto& modbus = ProductionModbus::GetInstance();
                       EnsureDeviceAddressIsConfigured(modbus);
                       uint16_t brightness =
                           static_cast<uint16_t>(properties["brightness_percent"].value<int>());

                       std::array<uint16_t, kLightCount> previous{};
                       esp_err_t err = modbus.ReadHoldingRegisters(kLight1Register, kLightCount,
                                                                   previous.data());
                       if (err != ESP_OK) {
                           throw MakeModbusError("Failed to read all light states", err);
                       }
                       for (std::size_t i = 0; i < kLightCount; ++i) {
                           WriteRegisterOrThrow(modbus, kLightRegisters[i], brightness,
                                                "Failed to set all lights");
                       }
                       auto actual = ReadBlockOrThrow<kLightCount>(
                           modbus, kLight1Register, "Failed to verify all light states");

                       cJSON* result = CreateSuccessResult();
                       cJSON_AddStringToObject(result, "write_mode", "sequential_0x06");
                       cJSON_AddNumberToObject(result, "requested_brightness_percent", brightness);
                       cJSON_AddNumberToObject(result, "start_register", kLight1Register);
                       cJSON_AddNumberToObject(result, "register_count", kLightCount);
                       cJSON* lights = cJSON_AddArrayToObject(result, "lights");
                       for (std::size_t i = 0; i < kLightCount; ++i) {
                           cJSON_AddItemToArray(
                               lights, CreateLightStatus(static_cast<uint8_t>(i + 1), actual[i]));
                       }
                       cJSON* previous_array =
                           cJSON_AddArrayToObject(result, "previous_raw_values");
                       for (uint16_t value : previous) {
                           cJSON_AddItemToArray(previous_array, cJSON_CreateNumber(value));
                       }
                       cJSON* actual_array = cJSON_AddArrayToObject(result, "raw_values");
                       for (uint16_t value : actual) {
                           cJSON_AddItemToArray(actual_array, cJSON_CreateNumber(value));
                       }
                       return result;
                   });

    server.AddTool(
        "self.production.light_schedule.get",
        "读取 4 路灯光的自动开/关灯时间（0x8D-0x94）。时间按「高字节小时、低字节分钟」编码，"
        "返回 HH:MM 与原始值。修改定时只影响自动计划，不会立即开关灯。",
        PropertyList(), [](const PropertyList&) -> ReturnValue {
            auto& modbus = ProductionModbus::GetInstance();
            EnsureDeviceAddressIsConfigured(modbus);
            auto values = ReadBlockOrThrow<kLightScheduleCount>(modbus, kLightScheduleStart,
                                                                "Failed to read light schedules");

            cJSON* result = CreateSuccessResult();
            cJSON_AddStringToObject(result, "encoding", "(hour << 8) | minute");
            cJSON* schedules = cJSON_AddArrayToObject(result, "schedules");
            for (std::size_t i = 0; i < kLightCount; ++i) {
                cJSON* item = cJSON_CreateObject();
                cJSON_AddNumberToObject(item, "channel", i + 1);
                cJSON_AddStringToObject(item, "on_time", FormatClockValue(values[i * 2]).c_str());
                cJSON_AddStringToObject(item, "off_time",
                                        FormatClockValue(values[i * 2 + 1]).c_str());
                cJSON_AddNumberToObject(item, "on_register", kLightScheduleStart + i * 2);
                cJSON_AddNumberToObject(item, "off_register", kLightScheduleStart + i * 2 + 1);
                cJSON_AddNumberToObject(item, "on_raw_value", values[i * 2]);
                cJSON_AddNumberToObject(item, "off_raw_value", values[i * 2 + 1]);
                cJSON_AddItemToArray(schedules, item);
            }
            AddRawValues(result, values, kLightScheduleStart);
            return result;
        });

    server.AddTool(
        "self.production.light_schedule.set",
        "设置指定一路灯光的自动开/关灯时间。on_time / off_time 格式 HH:MM，传空字符串表示不修改。"
        "只更新自动计划，不会立即改变灯光；即时控制请用灯光亮度工具。",
        PropertyList({Property("channel", kPropertyTypeInteger, 1, kLightCount),
                      Property("on_time", kPropertyTypeString, std::string("")),
                      Property("off_time", kPropertyTypeString, std::string(""))}),
        [](const PropertyList& properties) -> ReturnValue {
            auto& modbus = ProductionModbus::GetInstance();
            EnsureDeviceAddressIsConfigured(modbus);
            int channel = properties["channel"].value<int>();
            std::string on_text = properties["on_time"].value<std::string>();
            std::string off_text = properties["off_time"].value<std::string>();
            uint16_t on_value = 0;
            uint16_t off_value = 0;
            if (!on_text.empty() && !ParseClockValue(on_text, on_value)) {
                throw std::runtime_error("on_time must be HH:MM (00:00-23:59)");
            }
            if (!off_text.empty() && !ParseClockValue(off_text, off_value)) {
                throw std::runtime_error("off_time must be HH:MM (00:00-23:59)");
            }

            uint16_t on_register = kLightScheduleStart + (channel - 1) * 2;
            uint16_t off_register = on_register + 1;
            auto previous =
                ReadBlockOrThrow<2>(modbus, on_register, "Failed to read light schedule");
            if (!on_text.empty()) {
                WriteRegisterOrThrow(modbus, on_register, on_value, "Failed to set light on time");
            }
            if (!off_text.empty()) {
                WriteRegisterOrThrow(modbus, off_register, off_value,
                                     "Failed to set light off time");
            }
            auto actual =
                ReadBlockOrThrow<2>(modbus, on_register, "Failed to verify light schedule");

            cJSON* result = CreateSuccessResult();
            cJSON_AddNumberToObject(result, "channel", channel);
            cJSON_AddStringToObject(result, "encoding", "(hour << 8) | minute");
            cJSON_AddStringToObject(result, "on_time", FormatClockValue(actual[0]).c_str());
            cJSON_AddStringToObject(result, "off_time", FormatClockValue(actual[1]).c_str());
            cJSON_AddNumberToObject(result, "on_register", on_register);
            cJSON_AddNumberToObject(result, "off_register", off_register);
            cJSON_AddNumberToObject(result, "on_raw_value", actual[0]);
            cJSON_AddNumberToObject(result, "off_raw_value", actual[1]);
            cJSON_AddNumberToObject(result, "previous_on_raw_value", previous[0]);
            cJSON_AddNumberToObject(result, "previous_off_raw_value", previous[1]);
            return result;
        });

    server.AddTool(
        "self.production.silos.get_info",
        "读取 1-4 号料塔当前重量，单位千克。每个重量为低字在前、高字在后的 32 位有符号数。",
        PropertyList(), [](const PropertyList&) -> ReturnValue {
            auto& modbus = ProductionModbus::GetInstance();
            EnsureDeviceAddressIsConfigured(modbus);

            std::array<std::array<uint16_t, 2>, 4> weight_words{};
            std::array<int32_t, 4> weights{};
            for (std::size_t i = 0; i < kSiloWeightRegisters.size(); ++i) {
                weights[i] =
                    ReadSiloWeight(modbus, kSiloWeightRegisters[i], weight_words[i].data());
            }

            cJSON* result = cJSON_CreateObject();
            cJSON_AddBoolToObject(result, "success", true);
            cJSON* silos = cJSON_AddArrayToObject(result, "silos");
            for (std::size_t i = 0; i < kSiloWeightRegisters.size(); ++i) {
                cJSON* silo = cJSON_CreateObject();
                cJSON_AddNumberToObject(silo, "silo", i + 1);
                cJSON_AddNumberToObject(silo, "weight_kg", weights[i]);
                cJSON_AddNumberToObject(silo, "start_register", kSiloWeightRegisters[i]);
                cJSON_AddNumberToObject(silo, "low_word", weight_words[i][0]);
                cJSON_AddNumberToObject(silo, "high_word", weight_words[i][1]);
                cJSON_AddItemToArray(silos, silo);
            }
            return result;
        });

    // =======================================================================
    // 设备信息与模块启用状态
    // =======================================================================
    server.AddTool(
        "self.production.device.get_info",
        "读取基础设备信息：0x00 版本号（只读）与 0x01 当前 RS485 从站地址。", PropertyList(),
        [](const PropertyList&) -> ReturnValue {
            auto& modbus = ProductionModbus::GetInstance();
            EnsureDeviceAddressIsConfigured(modbus);
            auto values =
                ReadBlockOrThrow<2>(modbus, kVersionRegister, "Failed to read device information");

            char version_hex[16];
            std::snprintf(version_hex, sizeof(version_hex), "0x%04X",
                          static_cast<unsigned>(values[0]));
            cJSON* result = CreateSuccessResult();
            cJSON_AddNumberToObject(result, "version", values[0]);
            cJSON_AddStringToObject(result, "version_hex", version_hex);
            cJSON_AddNumberToObject(result, "device_address", values[1]);
            cJSON_AddNumberToObject(result, "local_device_address", modbus.GetDeviceAddress());
            AddRawValues(result, values, kVersionRegister);
            return result;
        });

    server.AddTool(
        "self.production.device.address.set",
        "修改 RS485 从站地址（1-127），写 0x01。成功后同步本地 Modbus 主站地址并用新地址回读"
        " 0x01 验证；回读失败会把本地主站回滚到原地址并报错，避免卡在失联的新 ID。"
        "写成功后旧地址不再应答，此操作会影响后续所有通讯。",
        PropertyList({Property("address", kPropertyTypeInteger, 1, kMaximumDeviceAddress)}),
        [](const PropertyList& properties) -> ReturnValue {
            auto& modbus = ProductionModbus::GetInstance();
            EnsureDeviceAddressIsConfigured(modbus);
            int address = properties["address"].value<int>();
            uint8_t previous_local_address = modbus.GetDeviceAddress();
            uint16_t previous = ReadRegisterOrThrow(modbus, kDeviceAddressRegister,
                                                    "Failed to read device address");
            WriteRegisterOrThrow(modbus, kDeviceAddressRegister, static_cast<uint16_t>(address),
                                 "Failed to set device address");
            esp_err_t err = modbus.SetDeviceAddress(static_cast<uint8_t>(address));
            if (err != ESP_OK) {
                throw MakeModbusError("Failed to update local Modbus address", err);
            }
            uint16_t verified = 0;
            try {
                verified = ReadRegisterOrThrow(modbus, kDeviceAddressRegister,
                                               "Failed to verify new device address");
            } catch (const std::exception&) {
                // Leave the master usable at the address that worked before the write.
                modbus.SetDeviceAddress(previous_local_address);
                throw std::runtime_error(
                    "Address write was sent but verification at the new ID failed; local master "
                    "rolled back to the previous address");
            }

            cJSON* result = CreateSuccessResult();
            cJSON_AddNumberToObject(result, "device_address", address);
            cJSON_AddNumberToObject(result, "previous_raw_value", previous);
            cJSON_AddNumberToObject(result, "verified_raw_value", verified);
            cJSON_AddBoolToObject(result, "verified_matches", verified == address);
            cJSON_AddNumberToObject(result, "register", kDeviceAddressRegister);
            return result;
        });

    server.AddTool("self.production.modules.get",
                   "读取 0xA6 模块启用配置位图：bit0 水线冲洗、bit1 水线检测、bit2 鸡蛋计数、"
                   "bit3 脱轨检测、bit4 料线系统、bit5 电压电能、bit6 水阀控制、bit7 称重系统、"
                   "bit8 物联网、bit9 灯光调光；1 启用、0 停用，bit10-15 未定义。",
                   PropertyList(), [](const PropertyList&) -> ReturnValue {
                       auto& modbus = ProductionModbus::GetInstance();
                       EnsureDeviceAddressIsConfigured(modbus);
                       uint16_t value = ReadRegisterOrThrow(modbus, kModuleStatusRegister,
                                                            "Failed to read module status");

                       cJSON* result = CreateSuccessResult();
                       cJSON_AddItemToObject(result, "modules",
                                             CreateBitField(value, kModuleStatusNames, 16, false));
                       cJSON_AddNumberToObject(result, "register", kModuleStatusRegister);
                       cJSON_AddNumberToObject(result, "raw_value", value);
                       return result;
                   });

    server.AddTool("self.production.log.get",
                   "读取 0x6A 运行日志代码：0 表示无数据，01-FF 分别对应不同的运行日志（具体编码由"
                   " 生产监控终端定义）。",
                   PropertyList(), [](const PropertyList&) -> ReturnValue {
                       auto& modbus = ProductionModbus::GetInstance();
                       EnsureDeviceAddressIsConfigured(modbus);
                       uint16_t value = ReadRegisterOrThrow(modbus, kDeviceLogRegister,
                                                            "Failed to read device log");

                       cJSON* result = CreateSuccessResult();
                       cJSON_AddBoolToObject(result, "has_log", value != 0);
                       cJSON_AddNumberToObject(result, "log_code", value);
                       cJSON_AddNumberToObject(result, "register", kDeviceLogRegister);
                       cJSON_AddNumberToObject(result, "raw_value", value);
                       return result;
                   });

    // =======================================================================
    // 料塔统计 / 水表 / 电压电能 / 系统状态
    // =======================================================================
    server.AddTool(
        "self.production.silos.get_statistics",
        "一次读取 1-4 号料塔的全部称重统计数据（0x02-0x31）：当前重量、实时加料量、"
        "实时卸料量、本日累计加料/卸料、加料/卸料总累计，单位千克。32 位数值低字在前。",
        PropertyList(), [](const PropertyList&) -> ReturnValue {
            auto& modbus = ProductionModbus::GetInstance();
            EnsureDeviceAddressIsConfigured(modbus);
            auto values = ReadBlockOrThrow<kSiloBlockCount>(modbus, kSiloBlockStart,
                                                            "Failed to read silo statistics");

            cJSON* result = CreateSuccessResult();
            cJSON* silos = cJSON_AddArrayToObject(result, "silos");
            for (std::size_t i = 0; i < kSiloCount; ++i) {
                const uint16_t* block = values.data() + i * kSiloBlockSize;
                cJSON* silo = cJSON_CreateObject();
                cJSON_AddNumberToObject(silo, "silo", i + 1);
                cJSON_AddNumberToObject(silo, "start_register",
                                        kSiloBlockStart + i * kSiloBlockSize);
                cJSON_AddNumberToObject(silo, "current_weight_kg", CombineSigned32(block));
                cJSON_AddNumberToObject(silo, "live_feed_kg", static_cast<int16_t>(block[2]));
                cJSON_AddNumberToObject(silo, "live_discharge_kg", static_cast<int16_t>(block[3]));
                cJSON_AddNumberToObject(silo, "daily_feed_kg", CombineSigned32(block + 4));
                cJSON_AddNumberToObject(silo, "daily_discharge_kg", CombineSigned32(block + 6));
                cJSON_AddNumberToObject(silo, "total_feed_kg", CombineSigned32(block + 8));
                cJSON_AddNumberToObject(silo, "total_discharge_kg", CombineSigned32(block + 10));
                cJSON_AddItemToArray(silos, silo);
            }
            AddRawValues(result, values, kSiloBlockStart);
            return result;
        });

    server.AddTool("self.production.water_meters.get",
                   "一次读取本日总用水量与 8 路水表读数（0x32-0x3A），单位吨。协议为自定义 2 字节"
                   "定点数，按 0.01 换算，范围 0.00-655.35t，非 IEEE754 浮点。",
                   PropertyList(), [](const PropertyList&) -> ReturnValue {
                       auto& modbus = ProductionModbus::GetInstance();
                       EnsureDeviceAddressIsConfigured(modbus);
                       auto values = ReadBlockOrThrow<kWaterMeterCount>(
                           modbus, kWaterMeterStart, "Failed to read water meters");

                       cJSON* result = CreateSuccessResult();
                       cJSON_AddStringToObject(result, "unit", "t");
                       cJSON_AddNumberToObject(result, "daily_total_tons",
                                               values[0] * kFixed100Scale);
                       cJSON* meters = cJSON_AddArrayToObject(result, "meters");
                       for (std::size_t i = 1; i < kWaterMeterCount; ++i) {
                           cJSON* meter = cJSON_CreateObject();
                           cJSON_AddNumberToObject(meter, "meter", i);
                           cJSON_AddNumberToObject(meter, "tons", values[i] * kFixed100Scale);
                           cJSON_AddNumberToObject(meter, "register", kWaterMeterStart + i);
                           cJSON_AddNumberToObject(meter, "raw_value", values[i]);
                           cJSON_AddItemToArray(meters, meter);
                       }
                       AddRawValues(result, values, kWaterMeterStart);
                       return result;
                   });

    server.AddTool(
        "self.production.power.get",
        "一次读取前端/后端操作间三相电压与两路电能计数（0x3F-0x46），16 位无符号原始值。",
        PropertyList(), [](const PropertyList&) -> ReturnValue {
            auto& modbus = ProductionModbus::GetInstance();
            EnsureDeviceAddressIsConfigured(modbus);
            auto values =
                ReadBlockOrThrow<kPowerCount>(modbus, kPowerStart, "Failed to read power data");

            cJSON* result = CreateSuccessResult();
            cJSON* front = cJSON_CreateObject();
            cJSON_AddNumberToObject(front, "a", values[0]);
            cJSON_AddNumberToObject(front, "b", values[1]);
            cJSON_AddNumberToObject(front, "c", values[2]);
            cJSON* rear = cJSON_CreateObject();
            cJSON_AddNumberToObject(rear, "a", values[3]);
            cJSON_AddNumberToObject(rear, "b", values[4]);
            cJSON_AddNumberToObject(rear, "c", values[5]);
            cJSON_AddItemToObject(result, "front_voltage", front);
            cJSON_AddItemToObject(result, "rear_voltage", rear);
            cJSON_AddNumberToObject(result, "front_energy", values[6]);
            cJSON_AddNumberToObject(result, "rear_energy", values[7]);
            AddRawValues(result, values, kPowerStart);
            return result;
        });

    server.AddTool(
        "self.production.system_status.get",
        "一次读取系统状态（0x47-0x4D）：鸡只数量（32 位无符号、低字在前）、每日死淘数量、"
        "水料比、鸡只耗料量、鸡只耗水量（后三者为 0.001 定点数）与 0x4D 时间原始值。",
        PropertyList(), [](const PropertyList&) -> ReturnValue {
            auto& modbus = ProductionModbus::GetInstance();
            EnsureDeviceAddressIsConfigured(modbus);
            auto values = ReadBlockOrThrow<kSystemStatusCount>(modbus, kSystemStatusStart,
                                                               "Failed to read system status");

            cJSON* result = CreateSuccessResult();
            cJSON_AddNumberToObject(result, "chicken_count", CombineUnsigned32(values.data()));
            cJSON_AddNumberToObject(result, "daily_mortality", values[2]);
            cJSON_AddNumberToObject(result, "water_feed_ratio", values[3] * kFixed1000Scale);
            cJSON_AddNumberToObject(result, "feed_consumption", values[4] * kFixed1000Scale);
            cJSON_AddNumberToObject(result, "water_consumption", values[5] * kFixed1000Scale);
            cJSON_AddNumberToObject(result, "time_raw_value", values[6]);
            AddRawValues(result, values, kSystemStatusStart);
            return result;
        });

    server.AddTool("self.production.daily_mortality.set",
                   "设置每日死淘数量（0x49，可读可写，16 位无符号）。写入前会读取旧值并返回。",
                   PropertyList({Property("count", kPropertyTypeInteger, 0, 65535)}),
                   [](const PropertyList& properties) -> ReturnValue {
                       auto& modbus = ProductionModbus::GetInstance();
                       EnsureDeviceAddressIsConfigured(modbus);
                       uint16_t value = static_cast<uint16_t>(properties["count"].value<int>());
                       uint16_t previous = ReadRegisterOrThrow(modbus, kDailyMortalityRegister,
                                                               "Failed to read daily mortality");
                       WriteRegisterOrThrow(modbus, kDailyMortalityRegister, value,
                                            "Failed to set daily mortality");

                       cJSON* result = CreateSuccessResult();
                       cJSON_AddNumberToObject(result, "daily_mortality", value);
                       cJSON_AddNumberToObject(result, "register", kDailyMortalityRegister);
                       cJSON_AddNumberToObject(result, "raw_value", value);
                       cJSON_AddNumberToObject(result, "previous_raw_value", previous);
                       return result;
                   });

    // =======================================================================
    // 报警区（一次读 25 个寄存器，便于屏幕弹框提示）
    // =======================================================================
    server.AddTool("self.production.alarms.get",
                   "一次读取全部报警寄存器 0x4E-0x66：各模块离线报警、一至八列缺水报警、"
                   "灯光过载/过热报警、称重传感器异常、脱轨检测/行车控制/鸡蛋计数离线报警。"
                   "0 表示无报警，0x1E 表示多个模块或多条水线报警。",
                   PropertyList(), [](const PropertyList&) -> ReturnValue {
                       auto& modbus = ProductionModbus::GetInstance();
                       EnsureDeviceAddressIsConfigured(modbus);
                       auto values = ReadBlockOrThrow<kAlarmCount>(modbus, kAlarmStart,
                                                                   "Failed to read alarms");

                       bool has_alarm = false;
                       cJSON* alarms = cJSON_CreateObject();
                       for (std::size_t i = 0; i < kAlarmCount; ++i) {
                           if (values[i] != 0) {
                               has_alarm = true;
                           }
                           std::string status = DescribeAlarm(values[i], kAlarmDefinitions[i]);
                           cJSON* item = cJSON_CreateObject();
                           cJSON_AddStringToObject(item, "status", status.c_str());
                           cJSON_AddBoolToObject(item, "active", values[i] != 0);
                           cJSON_AddNumberToObject(item, "register", kAlarmStart + i);
                           cJSON_AddNumberToObject(item, "raw_value", values[i]);
                           cJSON_AddItemToObject(alarms, kAlarmDefinitions[i].key, item);
                       }

                       cJSON* result = CreateSuccessResult();
                       cJSON_AddBoolToObject(result, "has_alarm", has_alarm);
                       cJSON_AddItemToObject(result, "alarms", alarms);
                       AddRawValues(result, values, kAlarmStart);
                       return result;
                   });

    server.AddTool("self.production.alarm_pause.set",
                   "暂停或恢复生产监控终端的缺水/离线/脱轨等报警（0x95）：低字节 01 暂停、00 恢复，"
                   "高字节为暂停时长（0-100 分钟）。",
                   PropertyList({Property("pause", kPropertyTypeBoolean),
                                 Property("minutes", kPropertyTypeInteger, 0, 0, 100)}),
                   [](const PropertyList& properties) -> ReturnValue {
                       auto& modbus = ProductionModbus::GetInstance();
                       EnsureDeviceAddressIsConfigured(modbus);
                       bool pause = properties["pause"].value<bool>();
                       int minutes = properties["minutes"].value<int>();
                       uint16_t previous = ReadRegisterOrThrow(modbus, kAlarmPauseRegister,
                                                               "Failed to read alarm pause state");
                       uint16_t value = static_cast<uint16_t>((minutes << 8) | (pause ? 1 : 0));
                       WriteRegisterOrThrow(modbus, kAlarmPauseRegister, value,
                                            "Failed to set alarm pause");

                       cJSON* result = CreateSuccessResult();
                       cJSON_AddBoolToObject(result, "paused", pause);
                       cJSON_AddNumberToObject(result, "minutes", minutes);
                       cJSON_AddNumberToObject(result, "register", kAlarmPauseRegister);
                       cJSON_AddNumberToObject(result, "raw_value", value);
                       cJSON_AddNumberToObject(result, "previous_raw_value", previous);
                       return result;
                   });

    // =======================================================================
    // 加药 / 水线冲洗 / 冲洗计划与参数
    // =======================================================================
    server.AddTool(
        "self.production.dosing.get",
        "读取 0x6D 加药状态：低 8 位为 8 路加药运行位（按协议表从高到低分别对应加药"
        " 1-8），高 8 位为加药时间参数（0-100 分钟）。",
        PropertyList(), [](const PropertyList&) -> ReturnValue {
            auto& modbus = ProductionModbus::GetInstance();
            EnsureDeviceAddressIsConfigured(modbus);
            uint16_t value =
                ReadRegisterOrThrow(modbus, kDosingRegister, "Failed to read dosing state");
            uint16_t channels = value & 0xFF;
            uint16_t duration = (value >> 8) & 0xFF;

            cJSON* result = CreateSuccessResult();
            cJSON_AddItemToObject(result, "channels",
                                  CreateBitField(channels, kDosingChannelNames, 8, true));
            cJSON_AddNumberToObject(result, "duration_minutes", duration);
            cJSON_AddNumberToObject(result, "register", kDosingRegister);
            cJSON_AddNumberToObject(result, "raw_value", value);
            return result;
        });

    server.AddTool(
        "self.production.dosing.set",
        "启动或停止指定一路加药（0x6D，channel 1-8）。duration_minutes 为可选加药时长"
        "（0-100，不传或传 -1 保持原值）；写入采用读-改-写，只改变目标通道位。",
        PropertyList(
            {Property("channel", kPropertyTypeInteger, 1, 8),
             Property("running", kPropertyTypeBoolean),
             Property("duration_minutes", kPropertyTypeInteger, -1, -1, kMaximumDosingMinutes)}),
        [](const PropertyList& properties) -> ReturnValue {
            auto& modbus = ProductionModbus::GetInstance();
            EnsureDeviceAddressIsConfigured(modbus);
            int channel = properties["channel"].value<int>();
            bool running = properties["running"].value<bool>();
            int duration = properties["duration_minutes"].value<int>();

            uint16_t previous =
                ReadRegisterOrThrow(modbus, kDosingRegister, "Failed to read dosing state");
            // 协议表「从高到低分别表示加药一…八」：通道 1 对应 bit7。
            uint8_t mask = static_cast<uint8_t>(1U << (8 - channel));
            uint8_t low = static_cast<uint8_t>(previous & 0xFF);
            low = running ? static_cast<uint8_t>(low | mask) : static_cast<uint8_t>(low & ~mask);
            uint8_t high = duration < 0 ? static_cast<uint8_t>((previous >> 8) & 0xFF)
                                        : static_cast<uint8_t>(duration);
            uint16_t value = static_cast<uint16_t>((static_cast<uint16_t>(high) << 8) | low);
            WriteRegisterOrThrow(modbus, kDosingRegister, value, "Failed to set dosing");

            cJSON* result = CreateSuccessResult();
            cJSON_AddNumberToObject(result, "channel", channel);
            cJSON_AddBoolToObject(result, "running", running);
            cJSON_AddNumberToObject(result, "channel_bit", 8 - channel);
            cJSON_AddNumberToObject(result, "duration_minutes", high);
            cJSON_AddNumberToObject(result, "register", kDosingRegister);
            cJSON_AddNumberToObject(result, "raw_value", value);
            cJSON_AddNumberToObject(result, "previous_raw_value", previous);
            return result;
        });

    server.AddTool(
        "self.production.flush.get",
        "一次读取水线冲洗全部信息（0x6E-0x7A）：运行状态（0 停止、1 冲洗、2 自动补水、"
        "3 高温换水）、下次自动冲洗周/时/分、单组冲洗时间、同时冲洗条数、冲洗位置列与 6 层。",
        PropertyList(), [](const PropertyList&) -> ReturnValue {
            auto& modbus = ProductionModbus::GetInstance();
            EnsureDeviceAddressIsConfigured(modbus);
            auto values = ReadBlockOrThrow<kFlushBlockCount>(modbus, kFlushBlockStart,
                                                             "Failed to read flush state");

            uint16_t state = values[0] & 0x03;
            cJSON* result = CreateSuccessResult();
            cJSON_AddStringToObject(result, "state", FlushStateName(state));
            cJSON_AddNumberToObject(result, "state_raw_value", state);
            cJSON_AddNumberToObject(result, "status_high_byte", (values[0] >> 8) & 0xFF);
            cJSON_AddNumberToObject(result, "next_schedule_week", values[1]);
            cJSON_AddNumberToObject(result, "next_schedule_hour", values[2]);
            cJSON_AddNumberToObject(result, "next_schedule_minute", values[3]);
            cJSON_AddNumberToObject(result, "group_duration", values[4]);
            cJSON_AddNumberToObject(result, "parallel_lines", values[5]);
            cJSON_AddNumberToObject(result, "flush_column", values[6]);
            cJSON* layers = cJSON_AddArrayToObject(result, "flush_layers");
            for (std::size_t i = 0; i < 6; ++i) {
                cJSON_AddItemToArray(layers, cJSON_CreateNumber(values[7 + i]));
            }
            cJSON_AddBoolToObject(result, "idle", state == 0);
            AddRawValues(result, values, kFlushBlockStart);
            return result;
        });

    server.AddTool(
        "self.production.flush.start",
        "启动水线冲洗（0x6E 写 01）。下发前必须先读取运行状态，设备不在停止状态时拒绝执行"
        "并提示「水线正在冲洗中（被占用）」。写入保留高字节只读位。",
        PropertyList(), [](const PropertyList&) -> ReturnValue {
            auto& modbus = ProductionModbus::GetInstance();
            EnsureDeviceAddressIsConfigured(modbus);
            uint16_t previous =
                ReadRegisterOrThrow(modbus, kFlushStateRegister, "Failed to read flush state");
            uint16_t state = previous & 0x03;
            if (state != 0) {
                throw std::runtime_error("Flush device is busy (" +
                                         std::string(FlushStateName(state)) +
                                         "); cannot start flushing");
            }
            uint16_t value = ReplaceLowByte(previous, 1);
            WriteRegisterOrThrow(modbus, kFlushStateRegister, value, "Failed to start flushing");

            cJSON* result = CreateSuccessResult();
            cJSON_AddStringToObject(result, "state", FlushStateName(1));
            cJSON_AddStringToObject(result, "previous_state", FlushStateName(state));
            cJSON_AddNumberToObject(result, "register", kFlushStateRegister);
            cJSON_AddNumberToObject(result, "raw_value", value);
            cJSON_AddNumberToObject(result, "previous_raw_value", previous);
            return result;
        });

    server.AddTool(
        "self.production.flush.stop", "停止水线冲洗（0x6E 写 00），写入保留高字节只读位。",
        PropertyList(), [](const PropertyList&) -> ReturnValue {
            auto& modbus = ProductionModbus::GetInstance();
            EnsureDeviceAddressIsConfigured(modbus);
            uint16_t previous =
                ReadRegisterOrThrow(modbus, kFlushStateRegister, "Failed to read flush state");
            uint16_t value = ReplaceLowByte(previous, 0);
            WriteRegisterOrThrow(modbus, kFlushStateRegister, value, "Failed to stop flushing");

            cJSON* result = CreateSuccessResult();
            cJSON_AddStringToObject(result, "state", FlushStateName(0));
            cJSON_AddStringToObject(result, "previous_state", FlushStateName(previous & 0x03));
            cJSON_AddNumberToObject(result, "register", kFlushStateRegister);
            cJSON_AddNumberToObject(result, "raw_value", value);
            cJSON_AddNumberToObject(result, "previous_raw_value", previous);
            return result;
        });

    server.AddTool(
        "self.production.flush_schedule.set",
        "设置下次自动冲洗时间（0x6F 周、0x70 时、0x71 分）。下发前读取冲洗状态，"
        "设备运行中则拒绝预约。语音「预约 30 分钟后冲洗」请由调用方换算为具体的周/时/分。",
        PropertyList({Property("week", kPropertyTypeInteger, 0, 7),
                      Property("hour", kPropertyTypeInteger, 0, 23),
                      Property("minute", kPropertyTypeInteger, 0, 59)}),
        [](const PropertyList& properties) -> ReturnValue {
            auto& modbus = ProductionModbus::GetInstance();
            EnsureDeviceAddressIsConfigured(modbus);
            int week = properties["week"].value<int>();
            int hour = properties["hour"].value<int>();
            int minute = properties["minute"].value<int>();

            uint16_t flush_state =
                ReadRegisterOrThrow(modbus, kFlushStateRegister, "Failed to read flush state");
            if ((flush_state & 0x03) != 0) {
                throw std::runtime_error("Flush device is running; cannot schedule a new flush");
            }
            auto previous = ReadBlockOrThrow<3>(modbus, kFlushScheduleWeekRegister,
                                                "Failed to read flush schedule");
            WriteRegisterOrThrow(modbus, kFlushScheduleWeekRegister, static_cast<uint16_t>(week),
                                 "Failed to set flush week");
            WriteRegisterOrThrow(modbus, kFlushScheduleHourRegister, static_cast<uint16_t>(hour),
                                 "Failed to set flush hour");
            WriteRegisterOrThrow(modbus, kFlushScheduleMinuteRegister,
                                 static_cast<uint16_t>(minute), "Failed to set flush minute");
            auto actual = ReadBlockOrThrow<3>(modbus, kFlushScheduleWeekRegister,
                                              "Failed to verify flush schedule");

            cJSON* result = CreateSuccessResult();
            cJSON_AddNumberToObject(result, "week", actual[0]);
            cJSON_AddNumberToObject(result, "hour", actual[1]);
            cJSON_AddNumberToObject(result, "minute", actual[2]);
            cJSON_AddNumberToObject(result, "start_register", kFlushScheduleWeekRegister);
            cJSON_AddNumberToObject(result, "register_count", 3);
            cJSON* previous_array = cJSON_AddArrayToObject(result, "previous_raw_values");
            for (uint16_t value : previous) {
                cJSON_AddItemToArray(previous_array, cJSON_CreateNumber(value));
            }
            cJSON* actual_array = cJSON_AddArrayToObject(result, "raw_values");
            for (uint16_t value : actual) {
                cJSON_AddItemToArray(actual_array, cJSON_CreateNumber(value));
            }
            return result;
        });

    server.AddTool(
        "self.production.flush_config.set",
        "修改一项冲洗参数。parameter 可取 group_duration(0-9999)、parallel_lines(1-7)、"
        "column(1-7)、layer_1..layer_6(1-168)；value 为对应取值。用于回答「现在的冲洗位置/"
        "冲洗时间/同时冲洗条数」类查询时请改用 flush.get。",
        PropertyList({Property("parameter", kPropertyTypeString),
                      Property("value", kPropertyTypeInteger, 0, 9999)}),
        [](const PropertyList& properties) -> ReturnValue {
            auto& modbus = ProductionModbus::GetInstance();
            EnsureDeviceAddressIsConfigured(modbus);
            std::string parameter = properties["parameter"].value<std::string>();
            int value = properties["value"].value<int>();

            const FlushConfigDefinition* definition = nullptr;
            for (const auto& candidate : kFlushConfigDefinitions) {
                if (parameter == candidate.name) {
                    definition = &candidate;
                    break;
                }
            }
            if (definition == nullptr) {
                throw std::runtime_error("Unknown flush parameter: " + parameter);
            }
            if (value < definition->minimum || value > definition->maximum) {
                throw std::runtime_error("Flush parameter out of range: " + parameter);
            }

            uint16_t previous = ReadRegisterOrThrow(modbus, definition->register_address,
                                                    "Failed to read flush configuration");
            WriteRegisterOrThrow(modbus, definition->register_address, static_cast<uint16_t>(value),
                                 "Failed to set flush configuration");

            cJSON* result = CreateSuccessResult();
            cJSON_AddStringToObject(result, "parameter", parameter.c_str());
            cJSON_AddNumberToObject(result, "value", value);
            cJSON_AddNumberToObject(result, "register", definition->register_address);
            cJSON_AddNumberToObject(result, "raw_value", value);
            cJSON_AddNumberToObject(result, "previous_raw_value", previous);
            return result;
        });

    // =======================================================================
    // 料线行车 / 料线模式与状态 / 脱轨
    // =======================================================================
    server.AddTool(
        "self.production.feed_cart.get",
        "一次读取料线行车全部信息（0x7B-0x82）：3F/2F/1F 行车状态与启动时间、行车模式"
        "（0 应急、1 自动、2 遥控）、喂料保护时间。行车状态低字节为状态码，高字节为 0-100 进度。",
        PropertyList(), [](const PropertyList&) -> ReturnValue {
            auto& modbus = ProductionModbus::GetInstance();
            EnsureDeviceAddressIsConfigured(modbus);
            auto values = ReadBlockOrThrow<kCartBlockCount>(modbus, kCartBlockStart,
                                                            "Failed to read feed cart state");

            static constexpr const char* kFloorNames[3] = {"3F", "2F", "1F"};
            cJSON* result = CreateSuccessResult();
            cJSON* carts = cJSON_AddArrayToObject(result, "carts");
            for (std::size_t i = 0; i < 3; ++i) {
                uint16_t state = values[i * 2] & 0xFF;
                cJSON* cart = cJSON_CreateObject();
                cJSON_AddStringToObject(cart, "floor", kFloorNames[i]);
                cJSON_AddStringToObject(cart, "state", CartStateName(state));
                cJSON_AddNumberToObject(cart, "state_code", state);
                cJSON_AddNumberToObject(cart, "progress_percent", (values[i * 2] >> 8) & 0xFF);
                cJSON_AddNumberToObject(cart, "start_time_minutes", values[i * 2 + 1]);
                cJSON_AddNumberToObject(cart, "state_register", kCartStateRegisters[2 - i]);
                cJSON_AddNumberToObject(cart, "start_time_register",
                                        kCartStartTimeRegisters[2 - i]);
                cJSON_AddItemToArray(carts, cart);
            }
            cJSON_AddStringToObject(result, "mode", CartModeName(values[6]));
            cJSON_AddNumberToObject(result, "mode_raw_value", values[6]);
            cJSON_AddBoolToObject(result, "cart_control_allowed", values[6] == 1 || values[6] == 2);
            cJSON_AddNumberToObject(result, "feed_protection_minutes", values[7]);
            AddRawValues(result, values, kCartBlockStart);
            return result;
        });

    server.AddTool(
        "self.production.feed_cart.start",
        "语音启动指定楼层行车（floor 1/2/3 对应 1F/2F/3F）。下发前读取行车模式与状态："
        "模式 0（应急）禁止控制；仅状态 1（前端到位）或 2（尾端到位）、模式 1/2 可启动，"
        "写入状态码 03（前进）。可选 protection_minutes 同时写 0x82 喂料保护时间"
        "（-1 表示不修改，公式「下次运行时间-本次运行时间+10min」需业务侧计算）。",
        PropertyList({Property("floor", kPropertyTypeInteger, 1, 3),
                      Property("protection_minutes", kPropertyTypeInteger, -1, -1, 65535)}),
        [](const PropertyList& properties) -> ReturnValue {
            auto& modbus = ProductionModbus::GetInstance();
            EnsureDeviceAddressIsConfigured(modbus);
            int floor = properties["floor"].value<int>();
            int protection = properties["protection_minutes"].value<int>();

            uint16_t mode =
                ReadRegisterOrThrow(modbus, kCartModeRegister, "Failed to read feed cart mode");
            if (mode == 0) {
                throw std::runtime_error("Feed cart is in emergency mode; control is blocked");
            }
            if (mode != 1 && mode != 2) {
                throw std::runtime_error("Feed cart mode does not allow voice control");
            }
            uint16_t register_address = CartStateRegister(floor);
            uint16_t previous =
                ReadRegisterOrThrow(modbus, register_address, "Failed to read feed cart state");
            uint16_t state = previous & 0xFF;
            if (state != 1 && state != 2) {
                throw std::runtime_error("Feed cart is not at an end position (" +
                                         std::string(CartStateName(state)) + "); cannot start");
            }
            uint16_t value = ReplaceLowByte(previous, 3);
            WriteRegisterOrThrow(modbus, register_address, value, "Failed to start feed cart");
            if (protection >= 0) {
                WriteRegisterOrThrow(modbus, kFeedProtectionRegister,
                                     static_cast<uint16_t>(protection),
                                     "Failed to set feed protection time");
            }

            cJSON* result = CreateSuccessResult();
            cJSON_AddNumberToObject(result, "floor", floor);
            cJSON_AddStringToObject(result, "state", CartStateName(3));
            cJSON_AddStringToObject(result, "previous_state", CartStateName(state));
            cJSON_AddStringToObject(result, "mode", CartModeName(mode));
            cJSON_AddNumberToObject(result, "register", register_address);
            cJSON_AddNumberToObject(result, "raw_value", value);
            cJSON_AddNumberToObject(result, "previous_raw_value", previous);
            cJSON_AddBoolToObject(result, "protection_updated", protection >= 0);
            if (protection >= 0) {
                cJSON_AddNumberToObject(result, "feed_protection_minutes", protection);
            }
            return result;
        });

    server.AddTool("self.production.feed_cart.start_time.set",
                   "设置指定楼层行车的启动时间（0x80=1F、0x7E=2F、0x7C=3F，16 位无符号分钟值）。",
                   PropertyList({Property("floor", kPropertyTypeInteger, 1, 3),
                                 Property("minutes", kPropertyTypeInteger, 0, 65535)}),
                   [](const PropertyList& properties) -> ReturnValue {
                       auto& modbus = ProductionModbus::GetInstance();
                       EnsureDeviceAddressIsConfigured(modbus);
                       int floor = properties["floor"].value<int>();
                       int minutes = properties["minutes"].value<int>();
                       uint16_t register_address = CartStartTimeRegister(floor);
                       uint16_t previous = ReadRegisterOrThrow(
                           modbus, register_address, "Failed to read feed cart start time");
                       WriteRegisterOrThrow(modbus, register_address,
                                            static_cast<uint16_t>(minutes),
                                            "Failed to set feed cart start time");

                       cJSON* result = CreateSuccessResult();
                       cJSON_AddNumberToObject(result, "floor", floor);
                       cJSON_AddNumberToObject(result, "start_time_minutes", minutes);
                       cJSON_AddNumberToObject(result, "register", register_address);
                       cJSON_AddNumberToObject(result, "raw_value", minutes);
                       cJSON_AddNumberToObject(result, "previous_raw_value", previous);
                       return result;
                   });

    server.AddTool(
        "self.production.feed_lines.get",
        "一次读取料线状态（0x83-0x89）：1-4 号料塔料线模式（0 无、1 定时补料、2 自动补料、"
        "3 暂停补料）、料塔料线运行位、舍内料线运行位、舍内料线超时报警位。"
        "位序按指令集文档 bit0 对应料线 1；控制逻辑表写作「从高到低」，两者不一致，"
        "故同时返回 raw_value 供现场核对。",
        PropertyList(), [](const PropertyList&) -> ReturnValue {
            auto& modbus = ProductionModbus::GetInstance();
            EnsureDeviceAddressIsConfigured(modbus);
            auto values = ReadBlockOrThrow<kFeedLineBlockCount>(modbus, kFeedLineBlockStart,
                                                                "Failed to read feed line state");

            cJSON* result = CreateSuccessResult();
            cJSON* modes = cJSON_AddArrayToObject(result, "silo_modes");
            for (std::size_t i = 0; i < 4; ++i) {
                cJSON* mode = cJSON_CreateObject();
                cJSON_AddNumberToObject(mode, "silo", i + 1);
                cJSON_AddStringToObject(mode, "mode", FeedLineModeName(values[i]));
                cJSON_AddNumberToObject(mode, "raw_value", values[i]);
                cJSON_AddNumberToObject(mode, "register", kFeedLineModeStart + i);
                cJSON_AddItemToArray(modes, mode);
            }
            cJSON_AddItemToObject(result, "silo_feed_lines",
                                  CreateBitField(values[4], kSiloFeedLineNames, 4, false));
            cJSON_AddItemToObject(result, "house_feed_lines",
                                  CreateBitField(values[5], kHouseFeedLineNames, 3, false));
            cJSON_AddItemToObject(result, "house_feed_line_alarms",
                                  CreateBitField(values[6], kHouseFeedLineNames, 3, false));
            cJSON_AddNumberToObject(result, "silo_status_register", kFeedLineSiloStatusRegister);
            cJSON_AddNumberToObject(result, "house_status_register", kFeedLineHouseStatusRegister);
            AddRawValues(result, values, kFeedLineBlockStart);
            return result;
        });

    server.AddTool(
        "self.production.feed_line_mode.set",
        "修改指定料塔的料线模式（0x83-0x86）：mode 0 无、1 定时补料、2 自动补料、3 暂停补料。"
        "对应语音「将料塔二的料线模式修改为自动补料」。",
        PropertyList({Property("silo", kPropertyTypeInteger, 1, 4),
                      Property("mode", kPropertyTypeInteger, 0, 3)}),
        [](const PropertyList& properties) -> ReturnValue {
            auto& modbus = ProductionModbus::GetInstance();
            EnsureDeviceAddressIsConfigured(modbus);
            int silo = properties["silo"].value<int>();
            int mode = properties["mode"].value<int>();
            uint16_t register_address = kFeedLineModeStart + (silo - 1);
            uint16_t previous =
                ReadRegisterOrThrow(modbus, register_address, "Failed to read feed line mode");
            WriteRegisterOrThrow(modbus, register_address, static_cast<uint16_t>(mode),
                                 "Failed to set feed line mode");

            cJSON* result = CreateSuccessResult();
            cJSON_AddNumberToObject(result, "silo", silo);
            cJSON_AddStringToObject(result, "mode", FeedLineModeName(mode));
            cJSON_AddStringToObject(result, "previous_mode", FeedLineModeName(previous));
            cJSON_AddNumberToObject(result, "register", register_address);
            cJSON_AddNumberToObject(result, "raw_value", mode);
            cJSON_AddNumberToObject(result, "previous_raw_value", previous);
            return result;
        });

    server.AddTool(
        "self.production.feed_lines.set",
        "直接设置料线运行位图（0x87 料塔 4 路或 0x88 舍内 3 路，位 0 对应料线 1）。"
        "target 取 silo 或 house，mask 为该位图（silo 0-15，house 0-7）。"
        "这是设备运行控制，调用前应先用 feed_lines.get 确认当前状态。",
        PropertyList({Property("target", kPropertyTypeString),
                      Property("mask", kPropertyTypeInteger, 0, 15)}),
        [](const PropertyList& properties) -> ReturnValue {
            auto& modbus = ProductionModbus::GetInstance();
            EnsureDeviceAddressIsConfigured(modbus);
            std::string target = properties["target"].value<std::string>();
            int mask = properties["mask"].value<int>();
            uint16_t register_address;
            int maximum;
            if (target == "silo") {
                register_address = kFeedLineSiloStatusRegister;
                maximum = 15;
            } else if (target == "house") {
                register_address = kFeedLineHouseStatusRegister;
                maximum = 7;
            } else {
                throw std::runtime_error("target must be 'silo' or 'house'");
            }
            if (mask > maximum) {
                throw std::runtime_error("mask exceeds the number of feed lines for " + target);
            }
            uint16_t previous =
                ReadRegisterOrThrow(modbus, register_address, "Failed to read feed line status");
            WriteRegisterOrThrow(modbus, register_address, static_cast<uint16_t>(mask),
                                 "Failed to set feed line status");

            cJSON* result = CreateSuccessResult();
            cJSON_AddStringToObject(result, "target", target.c_str());
            cJSON_AddNumberToObject(result, "mask", mask);
            cJSON_AddNumberToObject(result, "register", register_address);
            cJSON_AddNumberToObject(result, "raw_value", mask);
            cJSON_AddNumberToObject(result, "previous_raw_value", previous);
            return result;
        });

    server.AddTool("self.production.derailment.get",
                   "读取 3F/2F/1F 行车脱轨报警（0x8A-0x8C）。每层 bit6-bit0 对应行车 1-7，"
                   "1 表示脱轨报警。用于回答「行车脱轨现在什么状态」。",
                   PropertyList(), [](const PropertyList&) -> ReturnValue {
                       auto& modbus = ProductionModbus::GetInstance();
                       EnsureDeviceAddressIsConfigured(modbus);
                       auto values = ReadBlockOrThrow<kDerailmentCount>(
                           modbus, kDerailmentStart, "Failed to read derailment alarms");

                       static constexpr const char* kFloorNames[3] = {"3F", "2F", "1F"};
                       bool has_alarm = false;
                       cJSON* result = CreateSuccessResult();
                       cJSON* floors = cJSON_AddArrayToObject(result, "floors");
                       for (std::size_t i = 0; i < kDerailmentCount; ++i) {
                           if (values[i] != 0) {
                               has_alarm = true;
                           }
                           cJSON* floor = cJSON_CreateObject();
                           cJSON_AddStringToObject(floor, "floor", kFloorNames[i]);
                           cJSON_AddNumberToObject(floor, "register", kDerailmentStart + i);
                           cJSON_AddNumberToObject(floor, "raw_value", values[i]);
                           cJSON_AddItemToObject(
                               floor, "carts",
                               CreateBitField(values[i], kDerailmentCartNames, 7, true));
                           cJSON_AddItemToArray(floors, floor);
                       }
                       cJSON_AddBoolToObject(result, "has_alarm", has_alarm);
                       AddRawValues(result, values, kDerailmentStart);
                       return result;
                   });

    // =======================================================================
    // 鸡蛋计数与产蛋率
    // =======================================================================
    server.AddTool(
        "self.production.eggs.get",
        "一次读取鸡蛋计数总量（0x96-0x97，32 位无符号、低字在前）与 1-14 列鸡蛋数"
        "（0x98-0xA5），并结合 0x47-0x48 鸡只数量给出产蛋率。产蛋率按"
        "「鸡蛋总量 / 鸡只数 × 100」计算，若总量为累计值请业务侧另行换算口径。",
        PropertyList(), [](const PropertyList&) -> ReturnValue {
            auto& modbus = ProductionModbus::GetInstance();
            EnsureDeviceAddressIsConfigured(modbus);
            auto eggs =
                ReadBlockOrThrow<kEggCount>(modbus, kEggStart, "Failed to read egg counters");
            auto chickens =
                ReadBlockOrThrow<2>(modbus, kChickenCountRegister, "Failed to read chicken count");

            uint32_t total = CombineUnsigned32(eggs.data());
            uint32_t chicken_count = CombineUnsigned32(chickens.data());
            double laying_rate =
                chicken_count == 0 ? 0.0 : static_cast<double>(total) / chicken_count * 100.0;

            cJSON* result = CreateSuccessResult();
            cJSON_AddNumberToObject(result, "egg_total", total);
            cJSON_AddNumberToObject(result, "chicken_count", chicken_count);
            cJSON_AddNumberToObject(result, "laying_rate_percent", laying_rate);
            cJSON_AddStringToObject(result, "laying_rate_basis", "egg_total / chicken_count * 100");
            cJSON* columns = cJSON_AddArrayToObject(result, "columns");
            for (std::size_t i = 0; i < 14; ++i) {
                cJSON* column = cJSON_CreateObject();
                cJSON_AddNumberToObject(column, "column", i + 1);
                cJSON_AddNumberToObject(column, "eggs", eggs[2 + i]);
                cJSON_AddNumberToObject(column, "register", kEggStart + 2 + i);
                cJSON_AddItemToArray(columns, column);
            }
            AddRawValues(result, eggs, kEggStart);
            return result;
        });
}

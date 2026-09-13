/*
 * @Date: 2026-09-11 18:52:55
 * @LastEditors: zhouke
 * @LastEditTime: 2026-09-13 10:52:12
 * @FilePath: \xiaozhi-esp32\main\boards\waveshare\esp32-p4-nano\production_mcp_tools.cc
 */
#include "production_mcp_tools.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

#include "esp_err.h"
#include "mcp_server.h"
#include "production_modbus.h"

namespace {
constexpr uint16_t kMainWaterValveRegister = 0x6B;
constexpr uint16_t kWaterTemperatureRegister = 0x6C;
constexpr uint16_t kMainWaterValveOpenValue = 1;
constexpr uint16_t kMainWaterValveClosedValue = 2;
constexpr uint16_t kLight1Register = 0x3B;
constexpr uint16_t kLight2Register = 0x3C;
constexpr uint16_t kMinimumBrightness = 0;
constexpr uint16_t kMaximumBrightness = 100;
constexpr std::array<uint16_t, 2> kLightRegisters = {kLight1Register, kLight2Register};
constexpr std::array<uint16_t, 4> kSiloWeightRegisters = {0x02, 0x0E, 0x1A, 0x26};
constexpr double kWaterTemperatureScale = 0.001;

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

    uint32_t raw_value = (static_cast<uint32_t>(words[1]) << 16) | words[0];
    return static_cast<int32_t>(raw_value);
}
}  // namespace

void InitializeProductionMcpTools() {
    static bool initialized = false;
    if (initialized) {
        return;
    }
    initialized = true;

    auto& server = McpServer::GetInstance();
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
                   "查看指定一路灯光状态和当前亮度百分比。channel 取值为 1 或 2。",
                   PropertyList({Property("channel", kPropertyTypeInteger, 1, 2)}),
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

    server.AddTool(
        "self.production.lights.get", "一次读取并返回灯光 1 和灯光 2 的开关状态及亮度百分比。",
        PropertyList(), [](const PropertyList&) -> ReturnValue {
            auto& modbus = ProductionModbus::GetInstance();
            EnsureDeviceAddressIsConfigured(modbus);
            std::array<uint16_t, 2> brightness{};
            esp_err_t err =
                modbus.ReadHoldingRegisters(kLight1Register, brightness.size(), brightness.data());
            if (err != ESP_OK) {
                throw MakeModbusError("Failed to read all light states", err);
            }

            cJSON* result = cJSON_CreateObject();
            cJSON_AddBoolToObject(result, "success", true);
            cJSON* lights = cJSON_AddArrayToObject(result, "lights");
            cJSON_AddItemToArray(lights, CreateLightStatus(1, brightness[0]));
            cJSON_AddItemToArray(lights, CreateLightStatus(2, brightness[1]));
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
}

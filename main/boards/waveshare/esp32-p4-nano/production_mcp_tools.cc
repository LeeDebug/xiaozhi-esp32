#include "production_mcp_tools.h"

#include <stdexcept>
#include <string>

#include "esp_err.h"
#include "mcp_server.h"
#include "production_modbus.h"

namespace {
constexpr uint16_t kMainWaterValveRegister = 0x67;
constexpr uint16_t kWaterTemperatureRegister = 0x68;
constexpr uint16_t kMainWaterValveOpenValue = 1;
constexpr uint16_t kMainWaterValveClosedValue = 2;
constexpr double kWaterTemperatureScale = 0.001;

void EnsureDeviceAddressIsConfigured(const ProductionModbus& modbus) {
    if (modbus.GetDeviceAddress() == ProductionModbus::kUnsetDeviceAddress) {
        throw std::runtime_error("Modbus device address is not configured");
    }
}

std::runtime_error MakeModbusError(const char* operation, esp_err_t err) {
    return std::runtime_error(std::string(operation) + ": " + esp_err_to_name(err));
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
        "打开或关闭主水阀。open=true 时向保持寄存器 0x67 写入 1，open=false 时写入 2。",
        PropertyList({Property("open", kPropertyTypeBoolean)}),
        [](const PropertyList& properties) -> ReturnValue {
            auto& modbus = ProductionModbus::GetInstance();
            EnsureDeviceAddressIsConfigured(modbus);

            bool open = properties["open"].value<bool>();
            uint16_t value = open ? kMainWaterValveOpenValue : kMainWaterValveClosedValue;
            esp_err_t err = modbus.WriteHoldingRegister(kMainWaterValveRegister, value);
            if (err != ESP_OK) {
                throw MakeModbusError("Failed to set main water valve", err);
            }

            cJSON* result = cJSON_CreateObject();
            cJSON_AddBoolToObject(result, "success", true);
            cJSON_AddStringToObject(result, "state", open ? "open" : "closed");
            cJSON_AddNumberToObject(result, "register", kMainWaterValveRegister);
            cJSON_AddNumberToObject(result, "raw_value", value);
            return result;
        });

    server.AddTool(
        "self.production.water_temperature.get",
        "读取保持寄存器 0x68 中缓存的水温。协议值按 0.001 摄氏度换算。",
        PropertyList(), [](const PropertyList&) -> ReturnValue {
            auto& modbus = ProductionModbus::GetInstance();
            EnsureDeviceAddressIsConfigured(modbus);

            uint16_t raw_value = 0;
            if (!modbus.GetCachedRegister(kWaterTemperatureRegister, raw_value)) {
                throw std::runtime_error("Water temperature is not available yet");
            }

            cJSON* result = cJSON_CreateObject();
            cJSON_AddBoolToObject(result, "success", true);
            cJSON_AddNumberToObject(result, "temperature_celsius",
                                    raw_value * kWaterTemperatureScale);
            cJSON_AddNumberToObject(result, "register", kWaterTemperatureRegister);
            cJSON_AddNumberToObject(result, "raw_value", raw_value);
            return result;
        });
}

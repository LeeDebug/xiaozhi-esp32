/**
 * @file production_modbus.cc
 * @brief ProductionModbus 实现: UART1 上 9600-8N1 RTU 主站, 按需读写 + 本地缓存.
 * @note 仅 TX/RX 引脚, 依赖 RS485 自动收发切换, 无 DE/RE; 写成功即更新缓存/代系计数.
 */
#include "production_modbus.h"

#include <algorithm>
#include <array>
#include <cinttypes>

#include "config.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "mbcontroller.h"

namespace {
constexpr char kTag[] = "ProductionModbus";
constexpr uint16_t kMaximumReadRegisters = 78;
constexpr uint8_t kReadHoldingRegistersFunction = 0x03;
constexpr uint8_t kWriteSingleRegisterFunction = 0x06;
}  // namespace

ProductionModbus& ProductionModbus::GetInstance() {
    static ProductionModbus instance;
    return instance;
}

esp_err_t ProductionModbus::Start() {
    bool expected = false;
    if (!started_.compare_exchange_strong(expected, true)) {
        return ESP_OK;
    }

    bus_mutex_ = xSemaphoreCreateMutex();
    cache_mutex_ = xSemaphoreCreateMutex();
    if (kAutomaticPollingEnabled) {
        task_stopped_ = xSemaphoreCreateBinary();
    }
    if (bus_mutex_ == nullptr || cache_mutex_ == nullptr ||
        (kAutomaticPollingEnabled && task_stopped_ == nullptr)) {
        CleanupAfterStartFailure();
        return ESP_ERR_NO_MEM;
    }

    mb_communication_info_t communication = {};
    communication.ser_opts.port = PRODUCTION_MODBUS_UART_PORT;
    communication.ser_opts.mode = MB_RTU;
    communication.ser_opts.baudrate = 9600;
    communication.ser_opts.parity = MB_PARITY_NONE;
    communication.ser_opts.uid = 0;
    communication.ser_opts.response_tout_ms = 1000;
    communication.ser_opts.data_bits = UART_DATA_8_BITS;
    communication.ser_opts.stop_bits = UART_STOP_BITS_1;

    esp_err_t err = mbc_master_create_serial(&communication, &master_handle_);
    if (err != ESP_OK || master_handle_ == nullptr) {
        CleanupAfterStartFailure();
        return err == ESP_OK ? ESP_ERR_INVALID_STATE : err;
    }

    err = uart_set_pin(PRODUCTION_MODBUS_UART_PORT, PRODUCTION_MODBUS_UART_TX_PIN,
                       PRODUCTION_MODBUS_UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        CleanupAfterStartFailure();
        return err;
    }

    // Only TX/RX pins are assigned. This expects an RS485 transceiver with
    // automatic direction control; no DE/RE (RTS) GPIO is available here.
    err = uart_set_mode(PRODUCTION_MODBUS_UART_PORT, UART_MODE_UART);
    if (err != ESP_OK) {
        CleanupAfterStartFailure();
        return err;
    }

    err = mbc_master_start(master_handle_);
    if (err != ESP_OK) {
        CleanupAfterStartFailure();
        return err;
    }

    if (kAutomaticPollingEnabled) {
        running_.store(true);
        if (xTaskCreate(PollingTaskEntry, "production_mb", kTaskStackSize, this, kTaskPriority,
                        &task_handle_) != pdPASS) {
            running_.store(false);
            CleanupAfterStartFailure();
            return ESP_ERR_NO_MEM;
        }
    }

    ESP_LOGI(kTag,
             "Started %s RTU master on UART%d, TX=%d RX=%d, 9600 8N1; device address is unset",
             kAutomaticPollingEnabled ? "polling" : "on-demand",
             static_cast<int>(PRODUCTION_MODBUS_UART_PORT),
             static_cast<int>(PRODUCTION_MODBUS_UART_TX_PIN),
             static_cast<int>(PRODUCTION_MODBUS_UART_RX_PIN));
    return ESP_OK;
}

void ProductionModbus::Stop() {
    if (!started_.exchange(false)) {
        return;
    }

    running_.store(false);
    if (task_handle_ != nullptr) {
        xTaskNotifyGive(task_handle_);
        xSemaphoreTake(task_stopped_, portMAX_DELAY);
    }

    if (master_handle_ != nullptr) {
        mbc_master_stop(master_handle_);
        mbc_master_delete(master_handle_);
        master_handle_ = nullptr;
    }
    if (bus_mutex_ != nullptr) {
        vSemaphoreDelete(bus_mutex_);
        bus_mutex_ = nullptr;
    }
    if (cache_mutex_ != nullptr) {
        vSemaphoreDelete(cache_mutex_);
        cache_mutex_ = nullptr;
    }
    if (task_stopped_ != nullptr) {
        vSemaphoreDelete(task_stopped_);
        task_stopped_ = nullptr;
    }
}

esp_err_t ProductionModbus::SetDeviceAddress(uint8_t address) {
    if (address > 247) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t old_address = device_address_.exchange(address);
    if (old_address != address) {
        InvalidateCache();
        if (task_handle_ != nullptr) {
            xTaskNotifyGive(task_handle_);
        }
        ESP_LOGI(kTag, "Device address set to %u", address);
    }
    return ESP_OK;
}

uint8_t ProductionModbus::GetDeviceAddress() const { return device_address_.load(); }

esp_err_t ProductionModbus::ReadHoldingRegister(uint16_t register_address, uint16_t& value) {
    return ReadHoldingRegisters(register_address, 1, &value);
}

esp_err_t ProductionModbus::ReadHoldingRegisters(uint16_t start, uint16_t count, uint16_t* values) {
    uint8_t address = device_address_.load();
    if (!started_.load() || master_handle_ == nullptr || address == kUnsetDeviceAddress) {
        return ESP_ERR_INVALID_STATE;
    }
    if (values == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    return ReadHoldingRegisters(address, start, count, values);
}

bool ProductionModbus::GetCachedRegister(uint16_t register_address, uint16_t& value) const {
    if (register_address >= kRegisterCount || cache_mutex_ == nullptr) {
        return false;
    }

    xSemaphoreTake(cache_mutex_, portMAX_DELAY);
    bool valid = cache_.valid[register_address];
    if (valid) {
        value = cache_.holding_registers[register_address];
    }
    xSemaphoreGive(cache_mutex_);
    return valid;
}

void ProductionModbus::GetSnapshot(StateSnapshot& snapshot) const {
    if (cache_mutex_ == nullptr) {
        snapshot = {};
        return;
    }

    xSemaphoreTake(cache_mutex_, portMAX_DELAY);
    snapshot = cache_;
    xSemaphoreGive(cache_mutex_);
}

esp_err_t ProductionModbus::WriteHoldingRegister(uint16_t register_address, uint16_t value) {
    uint8_t address = device_address_.load();
    if (!started_.load() || master_handle_ == nullptr || address == kUnsetDeviceAddress) {
        return ESP_ERR_INVALID_STATE;
    }
    if (register_address >= kRegisterCount) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(bus_mutex_, pdMS_TO_TICKS(kBusLockTimeoutMs)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    mb_param_request_t request = {};
    request.slave_addr = address;
    request.command = kWriteSingleRegisterFunction;
    request.reg_start = register_address;
    request.reg_size = 1;
    esp_err_t err = mbc_master_send_request(master_handle_, &request, &value);
    xSemaphoreGive(bus_mutex_);

    if (err == ESP_OK && register_address < kRegisterCount && device_address_.load() == address) {
        xSemaphoreTake(cache_mutex_, portMAX_DELAY);
        cache_.holding_registers[register_address] = value;
        cache_.valid[register_address] = true;
        cache_.last_update_tick = xTaskGetTickCount();
        ++cache_.generation;
        xSemaphoreGive(cache_mutex_);
    }
    return err;
}

void ProductionModbus::PollingTaskEntry(void* context) {
    static_cast<ProductionModbus*>(context)->PollingTask();
}

void ProductionModbus::PollingTask() {
    uint32_t cycle = 0;
    uint32_t consecutive_failures = 0;

    while (running_.load()) {
        uint8_t address = device_address_.load();
        if (address == kUnsetDeviceAddress) {
            cycle = 0;
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(kAlarmPollIntervalMs));
            continue;
        }

        esp_err_t alarm_err = ReadHoldingRegisters(address, kAlarmStart, kAlarmCount, nullptr);
        esp_err_t water_err =
            ReadHoldingRegisters(address, kWaterStatusStart, kWaterStatusCount, nullptr);
        if ((cycle % kFullPollDivider) == 0) {
            esp_err_t general_err =
                ReadHoldingRegisters(address, kGeneralStart, kGeneralCount, nullptr);
            esp_err_t system_err =
                ReadHoldingRegisters(address, kSystemStart, kSystemCount, nullptr);
            if (general_err != ESP_OK || system_err != ESP_OK) {
                ESP_LOGW(kTag, "Full state poll incomplete: general=%s system=%s",
                         esp_err_to_name(general_err), esp_err_to_name(system_err));
            }
        }

        if (alarm_err == ESP_OK && water_err == ESP_OK) {
            if (consecutive_failures != 0) {
                ESP_LOGI(kTag,
                         "Modbus communication recovered after %" PRIu32 " failed status polls",
                         consecutive_failures);
            }
            consecutive_failures = 0;
        } else {
            ++consecutive_failures;
            if (consecutive_failures == 1 || (consecutive_failures % 10) == 0) {
                ESP_LOGW(kTag, "Status poll failed (%" PRIu32 "): alarm=%s water=%s",
                         consecutive_failures, esp_err_to_name(alarm_err),
                         esp_err_to_name(water_err));
            }
        }

        ++cycle;
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(kAlarmPollIntervalMs));
    }

    task_handle_ = nullptr;
    xSemaphoreGive(task_stopped_);
    vTaskDelete(nullptr);
}

esp_err_t ProductionModbus::ReadHoldingRegisters(uint8_t device_address, uint16_t start,
                                                 uint16_t count, uint16_t* values) {
    if (count == 0 || count > kMaximumReadRegisters || start + count > kRegisterCount) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(bus_mutex_, pdMS_TO_TICKS(kBusLockTimeoutMs)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    std::array<uint16_t, kMaximumReadRegisters> register_values{};
    mb_param_request_t request = {};
    request.slave_addr = device_address;
    request.command = kReadHoldingRegistersFunction;
    request.reg_start = start;
    request.reg_size = count;
    esp_err_t err = mbc_master_send_request(master_handle_, &request, register_values.data());
    xSemaphoreGive(bus_mutex_);

    if (err != ESP_OK) {
        return err;
    }
    if (values != nullptr) {
        std::copy_n(register_values.begin(), count, values);
    }
    if (device_address_.load() == device_address) {
        xSemaphoreTake(cache_mutex_, portMAX_DELAY);
        std::copy_n(register_values.begin(), count, cache_.holding_registers.begin() + start);
        std::fill_n(cache_.valid.begin() + start, count, true);
        cache_.last_update_tick = xTaskGetTickCount();
        ++cache_.generation;
        xSemaphoreGive(cache_mutex_);
    }
    return ESP_OK;
}

void ProductionModbus::InvalidateCache() {
    if (cache_mutex_ == nullptr) {
        cache_ = {};
        return;
    }

    xSemaphoreTake(cache_mutex_, portMAX_DELAY);
    cache_ = {};
    xSemaphoreGive(cache_mutex_);
}

void ProductionModbus::CleanupAfterStartFailure() {
    running_.store(false);
    if (master_handle_ != nullptr) {
        mbc_master_delete(master_handle_);
        master_handle_ = nullptr;
    }
    if (bus_mutex_ != nullptr) {
        vSemaphoreDelete(bus_mutex_);
        bus_mutex_ = nullptr;
    }
    if (cache_mutex_ != nullptr) {
        vSemaphoreDelete(cache_mutex_);
        cache_mutex_ = nullptr;
    }
    if (task_stopped_ != nullptr) {
        vSemaphoreDelete(task_stopped_);
        task_stopped_ = nullptr;
    }
    started_.store(false);
}

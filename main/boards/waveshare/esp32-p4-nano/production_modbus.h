#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

class ProductionModbus {
public:
    // The protocol document defines holding registers 0 through 166. Addresses
    // 103 and 104 are water controls/status; reserved address 105 is skipped.
    static constexpr size_t kRegisterCount = 167;
    static constexpr uint8_t kUnsetDeviceAddress = 0;

    struct StateSnapshot {
        std::array<uint16_t, kRegisterCount> holding_registers{};
        std::array<bool, kRegisterCount> valid{};
        TickType_t last_update_tick = 0;
        uint32_t generation = 0;
    };

    static ProductionModbus& GetInstance();

    esp_err_t Start();
    void Stop();

    // Address 0 means "not configured". Polling and writes remain disabled
    // until a unicast Modbus address (1..247) is supplied.
    esp_err_t SetDeviceAddress(uint8_t address);
    uint8_t GetDeviceAddress() const;

    // Returns false until the requested register has been read successfully.
    bool GetCachedRegister(uint16_t register_address, uint16_t& value) const;
    void GetSnapshot(StateSnapshot& snapshot) const;

    // The write uses the same bus mutex as the polling task, so an RTU request
    // can never overlap a periodic read. The call blocks until the request
    // completes or the configured Modbus response timeout expires.
    esp_err_t WriteHoldingRegister(uint16_t register_address, uint16_t value);

private:
    ProductionModbus() = default;
    ~ProductionModbus() = default;
    ProductionModbus(const ProductionModbus&) = delete;
    ProductionModbus& operator=(const ProductionModbus&) = delete;

    static constexpr uint16_t kGeneralStart = 0;
    static constexpr uint16_t kGeneralCount = 78;
    static constexpr uint16_t kAlarmStart = 78;
    static constexpr uint16_t kAlarmCount = 25;
    static constexpr uint16_t kWaterStatusStart = 0x67;
    static constexpr uint16_t kWaterStatusCount = 2;
    static constexpr uint16_t kSystemStart = 106;
    static constexpr uint16_t kSystemCount = 61;
    static constexpr uint32_t kAlarmPollIntervalMs = 1000;
    static constexpr uint32_t kFullPollDivider = 5;
    static constexpr uint32_t kBusLockTimeoutMs = 1500;
    static constexpr size_t kTaskStackSize = 4096;
    static constexpr UBaseType_t kTaskPriority = 5;

    static void PollingTaskEntry(void* context);
    void PollingTask();
    esp_err_t ReadHoldingRegisters(uint8_t device_address, uint16_t start, uint16_t count);
    void InvalidateCache();
    void CleanupAfterStartFailure();

    void* master_handle_ = nullptr;
    SemaphoreHandle_t bus_mutex_ = nullptr;
    mutable SemaphoreHandle_t cache_mutex_ = nullptr;
    SemaphoreHandle_t task_stopped_ = nullptr;
    TaskHandle_t task_handle_ = nullptr;
    std::atomic<uint8_t> device_address_{kUnsetDeviceAddress};
    std::atomic<bool> started_{false};
    std::atomic<bool> running_{false};
    StateSnapshot cache_;
};

#pragma once
/**
 * @file production_modbus.h
 * @brief 产线 Modbus-RTU 主站单例:保持寄存器 0..166 缓存 + 按需读写.
 *
 * - 地址 0 = 未配置, 读写/轮询均禁用, 直到 `SetDeviceAddress(1..247)`.
 * - 线程安全: 总线互斥 + 缓存互斥; 读/写共用总线锁, 不可重叠.
 * - 当前 `kAutomaticPollingEnabled=false`, 为请求驱动模式, 轮询任务保留未启用.
 * - 寄存器分区: 通用 0..77 / 告警 78..102 / 水 0x6B..0x6C / 系统 106..166(跳过保留 105).
 */

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

    // Reads one holding register on demand and updates its cached value.
    esp_err_t ReadHoldingRegister(uint16_t register_address, uint16_t& value);
    esp_err_t ReadHoldingRegisters(uint16_t start, uint16_t count, uint16_t* values);

    // Returns false until the requested register has been read successfully.
    bool GetCachedRegister(uint16_t register_address, uint16_t& value) const;
    void GetSnapshot(StateSnapshot& snapshot) const;

    // Reads and writes use the same bus mutex, so RTU requests cannot overlap.
    // The call blocks until the request completes or the configured Modbus
    // response timeout expires.
    esp_err_t WriteHoldingRegister(uint16_t register_address, uint16_t value);

private:
    ProductionModbus() = default;
    ~ProductionModbus() = default;
    ProductionModbus(const ProductionModbus&) = delete;
    ProductionModbus& operator=(const ProductionModbus&) = delete;

    // Keep the polling implementation available for later use, but do not
    // create the polling task while Modbus access is request-driven.
    static constexpr bool kAutomaticPollingEnabled = false;
    static constexpr uint16_t kGeneralStart = 0;
    static constexpr uint16_t kGeneralCount = 78;
    static constexpr uint16_t kAlarmStart = 78;
    static constexpr uint16_t kAlarmCount = 25;
    static constexpr uint16_t kWaterStatusStart = 0x6B;
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
    esp_err_t ReadHoldingRegisters(uint8_t device_address, uint16_t start, uint16_t count,
                                   uint16_t* values);
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

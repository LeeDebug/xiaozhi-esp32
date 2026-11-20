#include "production_history.h"

#include <esp_log.h>
#include <nvs.h>

#include <cstdio>
#include <ctime>
#include <numeric>

#include "application.h"
#include "herdsman_lcd_display.h"
#include "production_modbus.h"

namespace ProductionHistory {
namespace {
constexpr char kTag[] = "ProductionHistory";
constexpr char kNamespace[] = "prod_history";
constexpr int64_t kSecondsPerDay = 24 * 60 * 60;
constexpr TickType_t kSampleInterval = pdMS_TO_TICKS(10 * 60 * 1000);
// The slot count is part of the NVS layout because the date is mapped modulo this value.
constexpr size_t kStoredDays = 8;
static_assert(kDays <= kStoredDays);
constexpr std::array<uint16_t, 4> kFeedRegisters = {0x06, 0x12, 0x1E, 0x2A};
constexpr uint16_t kFirstWaterMeter = 0x33;
constexpr uint16_t kWaterMeterCount = 8;
constexpr uint16_t kEggStart = 0x96;
constexpr uint16_t kEggWords = 16;
static_assert(kFeedRegisters.back() + 2 <= ProductionModbus::kRegisterBlockSize);
static_assert(kFirstWaterMeter + kWaterMeterCount <= ProductionModbus::kRegisterBlockSize);
static_assert(kEggStart + kEggWords <= ProductionModbus::kRegisterCount);

using History = std::array<Day, kStoredDays>;

int64_t LocalMidnight() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    if (localtime_r(&now, &local) == nullptr || local.tm_year < 125) {
        return 0;
    }
    // The board uses a fixed CST-8 timezone without daylight-saving changes.
    return static_cast<int64_t>(now) - local.tm_hour * 3600 - local.tm_min * 60 - local.tm_sec;
}

size_t SlotFor(int64_t midnight) {
    return static_cast<size_t>((midnight / kSecondsPerDay) % kStoredDays);
}

uint32_t ReadUint32(const uint16_t* words) {
    return (static_cast<uint32_t>(words[1]) << 16) | words[0];
}

Day ReadToday(int64_t midnight) {
    Day sample{midnight};
    auto& modbus = ProductionModbus::GetInstance();
    const auto address = modbus.GetDeviceAddress();
    std::array<uint16_t, ProductionModbus::kRegisterBlockSize> first_block{};
    const auto first_result =
        modbus.ReadHoldingRegisters(0, first_block.size(), first_block.data());
    if (LocalMidnight() != midnight || modbus.GetDeviceAddress() != address) {
        return {};
    }
    if (first_result == ESP_OK) {
        int64_t feed = 0;
        for (const auto reg : kFeedRegisters) {
            feed += static_cast<int32_t>(ReadUint32(first_block.data() + reg));
        }
        sample.values[kFeed] = feed;
        const auto first_meter = first_block.begin() + kFirstWaterMeter;
        sample.values[kWater] = std::accumulate(first_meter, first_meter + kWaterMeterCount, 0LL);
    } else {
        ESP_LOGW(kTag, "Feed/water read failed: %s", esp_err_to_name(first_result));
    }

    std::array<uint16_t, kEggWords> eggs{};
    const auto egg_result = modbus.ReadHoldingRegisters(kEggStart, eggs.size(), eggs.data());
    if (modbus.GetDeviceAddress() != address) {
        return {};
    }
    if (egg_result == ESP_OK && LocalMidnight() == midnight) {
        // The first two words are the total. The remaining 14 are its individual counters.
        sample.values[kEggs] = ReadUint32(eggs.data());
    } else if (egg_result != ESP_OK) {
        ESP_LOGW(kTag, "Egg count read failed: %s", esp_err_to_name(egg_result));
    }
    return sample;
}

History LoadHistory(nvs_handle_t storage) {
    History days{};
    for (size_t i = 0; i < days.size(); ++i) {
        char key[16];
        std::snprintf(key, sizeof(key), "day%u", static_cast<unsigned>(i));
        size_t size = sizeof(Day);
        Day day;
        const auto result = nvs_get_blob(storage, key, &day, &size);
        if (result == ESP_OK && size == sizeof(day)) {
            days[i] = day;
        } else if (result != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(kTag, "Could not load %s: %s (size=%u)", key, esp_err_to_name(result),
                     static_cast<unsigned>(size));
        }
    }
    return days;
}

bool SaveSample(nvs_handle_t storage, History& history, const Day& sample) {
    if (sample.midnight == 0 || sample.values == Day{}.values) {
        return false;
    }
    const size_t slot = SlotFor(sample.midnight);
    Day& day = history[slot];
    if (day.midnight > sample.midnight) {
        return false;
    }
    if (day.midnight != sample.midnight) {
        day = Day{sample.midnight};
    }
    for (size_t i = 0; i < day.values.size(); ++i) {
        if (sample.values[i] != kMissing) {
            day.values[i] = sample.values[i];
        }
    }
    if (storage != 0) {
        char key[16];
        std::snprintf(key, sizeof(key), "day%u", static_cast<unsigned>(slot));
        auto result = nvs_set_blob(storage, key, &day, sizeof(day));
        if (result == ESP_OK) {
            result = nvs_commit(storage);
        }
        if (result != ESP_OK) {
            ESP_LOGW(kTag, "Could not save daily values: %s", esp_err_to_name(result));
        }
    }
    return true;
}

Week CurrentWeek(const History& history, int64_t today) {
    Week week{};
    for (size_t i = 0; i < week.size(); ++i) {
        const int64_t midnight = today - static_cast<int64_t>(week.size() - 1 - i) * kSecondsPerDay;
        const Day& day = history[SlotFor(midnight)];
        week[i] = day.midnight == midnight ? day : Day{midnight};
    }
    return week;
}

void Worker(void* context) {
    auto* display = static_cast<HerdsmanLcdDisplay*>(context);
    nvs_handle_t storage = 0;
    const auto result = nvs_open(kNamespace, NVS_READWRITE, &storage);
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "Could not open history storage: %s", esp_err_to_name(result));
    }
    History history = storage != 0 ? LoadHistory(storage) : History{};
    int64_t shown_day = 0;
    TickType_t last_sample = 0;
    bool sampled = false;

    while (true) {
        const int64_t today = LocalMidnight();
        if (today != 0) {
            bool refresh = shown_day != today;
            const TickType_t now = xTaskGetTickCount();
            if (!sampled || now - last_sample >= kSampleInterval) {
                sampled = true;
                last_sample = now;
                if (SaveSample(storage, history, ReadToday(today))) {
                    refresh = true;
                }
            }
            if (refresh && LocalMidnight() == today) {
                shown_day = today;
                Application::GetInstance().Schedule([display, week = CurrentWeek(history, today)] {
                    display->SetProductionHistory(week);
                });
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
}  // namespace

void Start(HerdsmanLcdDisplay& display) {
    if (xTaskCreate(Worker, "prod_history", 4096, &display, 2, nullptr) != pdPASS) {
        ESP_LOGE(kTag, "Could not start production history worker");
    }
}
}  // namespace ProductionHistory

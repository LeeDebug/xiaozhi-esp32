#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

class HerdsmanLcdDisplay;

namespace ProductionHistory {
constexpr size_t kDays = 8;
constexpr int64_t kMissing = std::numeric_limits<int64_t>::min();
enum Metric { kFeed, kWater, kEggs, kMetricCount };

struct Day {
    int64_t midnight = 0;
    // Feed in kg, water in hundredths of a tonne, and eggs as a count.
    std::array<int64_t, kMetricCount> values = {kMissing, kMissing, kMissing};
};

using Week = std::array<Day, kDays>;

void Start(HerdsmanLcdDisplay& display);
}  // namespace ProductionHistory

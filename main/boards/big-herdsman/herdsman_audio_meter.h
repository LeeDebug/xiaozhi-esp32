#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>

// Board-local telemetry: audio tasks publish levels; only the LVGL task touches widgets.
class HerdsmanAudioMeter {
public:
    static void Publish(const std::vector<int16_t>& pcm, int channels, bool playback) {
        if (pcm.empty()) {
            return;
        }
        channels = std::max(1, channels);
        uint64_t sum = 0;
        size_t count = 0;
        // Channel zero is the microphone; skip the AEC reference channel.
        for (size_t i = 0; i < pcm.size(); i += channels) {
            const int sample = pcm[i];
            sum += sample < 0 ? -sample : sample;
            ++count;
        }
        const int level = std::min<uint64_t>(1000, sum * 1000 / (count * 8192));
        auto& target = playback ? output_ : input_;
        int previous = target.load(std::memory_order_relaxed);
        while (previous < level &&
               !target.compare_exchange_weak(previous, level, std::memory_order_relaxed)) {
        }
    }

    static int Consume(bool playback) {
        return (playback ? output_ : input_).exchange(0, std::memory_order_relaxed);
    }

private:
    inline static std::atomic<int> input_{0};
    inline static std::atomic<int> output_{0};
};

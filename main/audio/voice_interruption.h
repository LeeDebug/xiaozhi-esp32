#ifndef VOICE_INTERRUPTION_H
#define VOICE_INTERRUPTION_H

#include <algorithm>
#include <cstdint>

// Called under AudioService's queue mutex. Only consecutive, freshly fetched
// speech frames count; neither retained VAD state nor a timer can trigger it.
class VoiceInterruption {
public:
    void Arm() {
        armed_ = true;
        pending_ = false;
        playback_start_us_ = 0;
        ResetSpeech();
    }
    void Disarm() {
        armed_ = false;
        pending_ = false;
        ResetSpeech();
    }
    void PlaybackStarted(int64_t now_us) {
        if (armed_ && playback_start_us_ == 0) {
            playback_start_us_ = now_us;
        }
    }
    bool Observe(bool speech, uint32_t frame_ms, int64_t now_us, uint32_t guard_ms,
                 uint32_t min_speech_ms) {
        if (!armed_ || pending_ || playback_start_us_ == 0 ||
            now_us - playback_start_us_ < static_cast<int64_t>(guard_ms) * 1000 || !speech ||
            frame_ms == 0) {
            ResetSpeech();
            return false;
        }
        const int64_t max_gap_us = std::max<int64_t>(100000, frame_ms * 2000);
        if (last_frame_us_ == 0 || now_us - last_frame_us_ > max_gap_us) {
            ResetSpeech();
            speech_start_us_ = now_us;
        }
        last_frame_us_ = now_us;
        speech_ms_ += frame_ms;
        if (speech_ms_ < min_speech_ms ||
            now_us - speech_start_us_ < static_cast<int64_t>(min_speech_ms) * 1000) {
            return false;
        }
        pending_ = true;
        return true;
    }
    bool pending() const { return armed_ && pending_; }

private:
    void ResetSpeech() {
        speech_ms_ = 0;
        speech_start_us_ = 0;
        last_frame_us_ = 0;
    }
    bool armed_ = false;
    bool pending_ = false;
    int64_t playback_start_us_ = 0;
    int64_t speech_start_us_ = 0;
    int64_t last_frame_us_ = 0;
    uint64_t speech_ms_ = 0;
};

#endif

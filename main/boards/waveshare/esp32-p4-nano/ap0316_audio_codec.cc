#include "config.h"

#if AUDIO_USE_AP0316

#include "ap0316_audio_codec.h"

#include "herdsman_audio_meter.h"

#include <esp_check.h>
#include <esp_log.h>
#include <freertos/task.h>

#include <algorithm>
#include <array>
#include <cassert>

namespace {
constexpr char kTag[] = "Ap0316AudioCodec";
constexpr uint32_t kIoTimeoutMs = 100;
constexpr uint32_t kOutputTimeoutMs = 20;
constexpr int kOutputChunkFrames = 240;
constexpr uint32_t kErrorLogIntervalMs = 5000;
}  // namespace

Ap0316AudioCodec::Ap0316AudioCodec(i2c_master_bus_handle_t i2c_bus, i2c_port_t i2c_port) {
    // AudioService uses duplex_ to keep shared TX clocks alive for RX. Here the
    // clocks are independent, so output may power down while input keeps running.
    duplex_ = false;
    input_reference_ = false;  // AP-0316 performs AEC using its analog LINE_IN.
    input_sample_rate_ = AUDIO_INPUT_SAMPLE_RATE;
    output_sample_rate_ = AUDIO_OUTPUT_SAMPLE_RATE;
    input_channels_ = 1;
    output_channels_ = 1;
    CreateChannels();

    audio_codec_i2s_cfg_t data_config = {};
    data_config.port = 0;
    data_config.tx_handle = tx_handle_;
    // Never give this interface the AP-0316 RX handle: opening the 24 kHz DAC
    // must not reconfigure the externally clocked 48 kHz receiver.
    data_if_ = audio_codec_new_i2s_data(&data_config);
    assert(data_if_ != nullptr);

    audio_codec_i2c_cfg_t control_config = {};
    control_config.port = i2c_port;
    control_config.addr = AUDIO_CODEC_ES8311_ADDR;
    control_config.bus_handle = i2c_bus;
    ctrl_if_ = audio_codec_new_i2c_ctrl(&control_config);
    assert(ctrl_if_ != nullptr);
    uint8_t reset_value = 0x1f;
    ESP_ERROR_CHECK(ctrl_if_->write_reg(ctrl_if_, 0x00, 1, &reset_value, 1));
    vTaskDelay(pdMS_TO_TICKS(5));

    gpio_if_ = audio_codec_new_gpio();
    assert(gpio_if_ != nullptr);
    es8311_codec_cfg_t codec_config = {};
    codec_config.ctrl_if = ctrl_if_;
    codec_config.gpio_if = gpio_if_;
    codec_config.codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC;
    codec_config.pa_pin = AUDIO_CODEC_PA_PIN;
    codec_config.use_mclk = true;
    codec_config.hw_gain.pa_voltage = 5.0;
    codec_config.hw_gain.codec_dac_voltage = 3.3;
    codec_if_ = es8311_codec_new(&codec_config);
    assert(codec_if_ != nullptr);

    esp_codec_dev_cfg_t device_config = {};
    device_config.dev_type = ESP_CODEC_DEV_TYPE_OUT;
    device_config.codec_if = codec_if_;
    device_config.data_if = data_if_;
    output_dev_ = esp_codec_dev_new(&device_config);
    assert(output_dev_ != nullptr);
    ESP_ERROR_CHECK(gpio_set_level(AUDIO_CODEC_PA_PIN, 0));
    ESP_ERROR_CHECK(i2s_channel_disable(tx_handle_));
    ESP_LOGI(kTag, "Mode 6: AP-0316 I2S1 slave RX 48 kHz, ES8311 I2S0 master TX 24 kHz");
}

Ap0316AudioCodec::~Ap0316AudioCodec() {
    EnableInput(false);
    EnableOutput(false);
    esp_codec_dev_delete(output_dev_);
    audio_codec_delete_codec_if(codec_if_);
    audio_codec_delete_ctrl_if(ctrl_if_);
    audio_codec_delete_gpio_if(gpio_if_);
    audio_codec_delete_data_if(data_if_);
    i2s_del_channel(rx_handle_);
    i2s_del_channel(tx_handle_);
}

void Ap0316AudioCodec::CreateChannels() {
    i2s_chan_config_t tx_config = I2S_CHANNEL_DEFAULT_CONFIG(XIAOZHI_I2S_PORT(0), I2S_ROLE_MASTER);
    tx_config.dma_desc_num = AUDIO_CODEC_DMA_DESC_NUM;
    tx_config.dma_frame_num = AUDIO_CODEC_DMA_FRAME_NUM;
    tx_config.auto_clear_after_cb = true;
    ESP_ERROR_CHECK(i2s_new_channel(&tx_config, &tx_handle_, nullptr));

    i2s_std_config_t tx_format = {};
    tx_format.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_OUTPUT_SAMPLE_RATE);
    tx_format.slot_cfg =
        I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    tx_format.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
    tx_format.gpio_cfg.mclk = AUDIO_I2S_GPIO_MCLK;
    tx_format.gpio_cfg.bclk = AUDIO_I2S_GPIO_BCLK;
    tx_format.gpio_cfg.ws = AUDIO_I2S_GPIO_WS;
    tx_format.gpio_cfg.dout = AUDIO_I2S_GPIO_DOUT;
    tx_format.gpio_cfg.din = I2S_GPIO_UNUSED;
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle_, &tx_format));
    // Supply MCLK during the ES8311 reset/initialization, as on the onboard path.
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle_));

    i2s_chan_config_t rx_config = I2S_CHANNEL_DEFAULT_CONFIG(XIAOZHI_I2S_PORT(1), I2S_ROLE_SLAVE);
    rx_config.dma_desc_num = AUDIO_CODEC_DMA_DESC_NUM;
    rx_config.dma_frame_num = AUDIO_CODEC_DMA_FRAME_NUM;
    ESP_ERROR_CHECK(i2s_new_channel(&rx_config, nullptr, &rx_handle_));

    i2s_std_config_t rx_format = {};
    rx_format.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_INPUT_SAMPLE_RATE);
    rx_format.slot_cfg =
        I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    // The manual specifies 3.072 MHz BCLK / 48 kHz WS = 64 bits per frame:
    // two 32-bit slots, with 16 significant bits of Philips-aligned PCM each.
    rx_format.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;
    rx_format.slot_cfg.ws_width = 32;
#if AP0316_INPUT_RIGHT_SLOT
    rx_format.slot_cfg.slot_mask = I2S_STD_SLOT_RIGHT;
#else
    rx_format.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
#endif
    rx_format.gpio_cfg.mclk = I2S_GPIO_UNUSED;
    rx_format.gpio_cfg.bclk = AP0316_I2S_GPIO_BCLK;
    rx_format.gpio_cfg.ws = AP0316_I2S_GPIO_WS;
    rx_format.gpio_cfg.dout = I2S_GPIO_UNUSED;
    rx_format.gpio_cfg.din = AP0316_I2S_GPIO_DIN;
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle_, &rx_format));
    ESP_LOGI(kTag, "AP-0316 RX: BCLK GPIO%d, WS GPIO%d, DIN GPIO%d, %s slot", AP0316_I2S_GPIO_BCLK,
             AP0316_I2S_GPIO_WS, AP0316_I2S_GPIO_DIN,
             rx_format.slot_cfg.slot_mask == I2S_STD_SLOT_LEFT ? "left" : "right");
}

void Ap0316AudioCodec::EnableInput(bool enable) {
    std::lock_guard<std::mutex> lock(input_mutex_);
    if (enable == input_enabled_) {
        return;
    }
    esp_err_t err = enable ? i2s_channel_enable(rx_handle_) : i2s_channel_disable(rx_handle_);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Cannot %s AP-0316 input: %s", enable ? "enable" : "disable",
                 esp_err_to_name(err));
        return;
    }
    AudioCodec::EnableInput(enable);
}

void Ap0316AudioCodec::EnableOutput(bool enable) {
    std::lock_guard<std::mutex> lock(output_mutex_);
    if (enable == output_enabled_) {
        return;
    }
    if (enable) {
        esp_codec_dev_sample_info_t format = {};
        format.bits_per_sample = 16;
        format.channel = 1;
        format.sample_rate = output_sample_rate_;
        // The codec data interface stops TX to reconfigure it, then enables it.
        ESP_ERROR_CHECK(i2s_channel_enable(tx_handle_));
        ESP_ERROR_CHECK(esp_codec_dev_open(output_dev_, &format));
        ESP_ERROR_CHECK(esp_codec_dev_set_out_vol(output_dev_, output_volume_));
    } else {
        ESP_ERROR_CHECK(esp_codec_dev_close(output_dev_));
    }
    // Explicitly silence the board PA while output is idle, independently of RX.
    ESP_ERROR_CHECK(gpio_set_level(AUDIO_CODEC_PA_PIN,
                                   enable && !output_cancelled_ && !output_flush_pending_ ? 1 : 0));
    if (output_cancelled_ || output_flush_pending_)
        gpio_set_level(AUDIO_CODEC_PA_PIN, 0);
    AudioCodec::EnableOutput(enable);
}

void Ap0316AudioCodec::SetOutputVolume(int volume) {
    std::lock_guard<std::mutex> lock(output_mutex_);
    volume = std::clamp(volume, 0, 100);
    if (output_enabled_) {
        ESP_ERROR_CHECK(esp_codec_dev_set_out_vol(output_dev_, volume));
    }
    AudioCodec::SetOutputVolume(volume);
}

bool Ap0316AudioCodec::InputData(std::vector<int16_t>& data) {
    if (data.empty() || Read(data.data(), data.size()) != static_cast<int>(data.size())) {
        return false;
    }
    HerdsmanAudioMeter::Publish(data, input_channels_, false);
    return true;
}

void Ap0316AudioCodec::OutputData(std::vector<int16_t>& data) { Write(data.data(), data.size()); }

int Ap0316AudioCodec::Read(int16_t* dest, int samples) {
    std::lock_guard<std::mutex> lock(input_mutex_);
    if (!input_enabled_ || samples <= 0) {
        return 0;
    }
    size_t received = 0;
    const size_t requested = samples * sizeof(int16_t);
    esp_err_t err = i2s_channel_read(rx_handle_, dest, requested, &received, kIoTimeoutMs);
    if (err != ESP_OK || received != requested) {
        // AudioService expects a complete vector; never feed a partial/stale
        // frame into wake word detection or the resampler after a missing clock.
        std::fill_n(dest, samples, 0);
        const TickType_t now = xTaskGetTickCount();
        if (!input_error_logged_ ||
            now - last_input_error_time_ >= pdMS_TO_TICKS(kErrorLogIntervalMs)) {
            ESP_LOGW(kTag, "Incomplete AP-0316 input (%u/%u bytes): %s; check power/BCLK/WS/D_OUT",
                     static_cast<unsigned>(received), static_cast<unsigned>(requested),
                     esp_err_to_name(err));
            last_input_error_time_ = now;
            input_error_logged_ = true;
        }
        return 0;
    }
    return samples;
}

bool Ap0316AudioCodec::HasHardwareAec() const { return AP0316_HAS_HARDWARE_AEC; }

VoiceInterruptionConfig Ap0316AudioCodec::GetVoiceInterruptionConfig() const {
    return {AP0316_HAS_HARDWARE_AEC && AUDIO_VOICE_INTERRUPTION_ENABLED,
            AUDIO_VOICE_INTERRUPTION_MIN_SPEECH_MS, AUDIO_VOICE_INTERRUPTION_PLAYBACK_GUARD_MS,
            AUDIO_VOICE_INTERRUPTION_PLAYBACK_DRAIN_MS};
}

void Ap0316AudioCodec::CancelOutput(uint32_t generation) {
    output_cancelled_ = true;
    output_generation_ = generation;
    output_flush_pending_ = true;
    // GPIO mute is immediate and does not wait for an outstanding I2S write.
    // Only TX is muted; the AP receiver and its external clocks continue.
    gpio_set_level(AUDIO_CODEC_PA_PIN, 0);
}

void Ap0316AudioCodec::ResumeOutput(uint32_t generation) {
    if (generation == output_generation_)
        output_cancelled_ = false;
}

void Ap0316AudioCodec::OutputData(std::vector<int16_t>& data, uint32_t generation) {
    WriteGeneration(data.data(), data.size(), generation);
}

int Ap0316AudioCodec::Write(const int16_t* data, int samples) {
    return WriteGeneration(data, samples, output_generation_);
}

int Ap0316AudioCodec::WriteGeneration(const int16_t* data, int samples, uint32_t generation) {
    std::lock_guard<std::mutex> lock(output_mutex_);
    auto cancelled = [this, generation]() {
        return output_cancelled_ || output_generation_ != generation;
    };
    if (!output_enabled_ || samples <= 0 || cancelled())
        return 0;
    if (output_flush_pending_.exchange(false)) {
        // Reset TX DMA while muted, replacing every descriptor with silence.
        // This work runs in the output task, never in CancelOutput/main task.
        gpio_set_level(AUDIO_CODEC_PA_PIN, 0);
        esp_err_t err = i2s_channel_disable(tx_handle_);
        if (err == ESP_ERR_INVALID_STATE) {
            // A previous preload may have failed while READY. A successful
            // enable/disable cycle resets its partial DMA preload cursor.
            err = i2s_channel_enable(tx_handle_);
            if (err == ESP_OK)
                err = i2s_channel_disable(tx_handle_);
        }
        std::array<int16_t, kOutputChunkFrames> silence{};
        i2s_chan_info_t info = {};
        if (err == ESP_OK)
            err = i2s_channel_get_info(tx_handle_, &info);
        size_t remaining = info.total_dma_buf_size;
        while (err == ESP_OK && remaining > 0) {
            size_t loaded = 0;
            err = i2s_channel_preload_data(tx_handle_, silence.data(),
                                           std::min(remaining, sizeof(silence)), &loaded);
            if (loaded == 0) {
                err = ESP_FAIL;
                break;
            }
            remaining -= loaded;
        }
        if (err == ESP_OK)
            err = i2s_channel_enable(tx_handle_);
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "Cannot reset cancelled ES8311 output: %s", esp_err_to_name(err));
            // Leave READY through a real enable transition so a future reset
            // can disable again and discard any partial preload position.
            // PA stays muted throughout this recovery, even if TX restarts.
            const esp_err_t recovery = i2s_channel_enable(tx_handle_);
            if (recovery != ESP_OK && recovery != ESP_ERR_INVALID_STATE) {
                ESP_LOGW(kTag, "ES8311 TX reset recovery failed: %s", esp_err_to_name(recovery));
            }
            output_flush_pending_ = true;
            return 0;
        }
    }
    if (cancelled())
        return 0;
    gpio_set_level(AUDIO_CODEC_PA_PIN, 1);
    int completed = 0;
    while (completed < samples && !cancelled()) {
        size_t written = 0;
        const int frames = std::min(samples - completed, kOutputChunkFrames);
        const esp_err_t err = i2s_channel_write(
            tx_handle_, data + completed, frames * sizeof(int16_t), &written, kOutputTimeoutMs);
        completed += written / sizeof(int16_t);
        if (err != ESP_OK || written != frames * sizeof(int16_t))
            break;
    }
    // Also covers cancellation between the final check and PA enable above.
    if (cancelled())
        gpio_set_level(AUDIO_CODEC_PA_PIN, 0);
    return completed;
}

#endif  // AUDIO_USE_AP0316

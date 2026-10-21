#pragma once

#include "audio_codec.h"

#include <driver/i2c_master.h>
#include <esp_codec_dev.h>
#include <esp_codec_dev_defaults.h>

#include <mutex>

// AP-0316 mode 6 supplies its own I2S clocks. The ES8311 DAC uses a separate
// controller and clock domain; neither device's clock pins are connected together.
class Ap0316AudioCodec : public AudioCodec {
public:
    Ap0316AudioCodec(i2c_master_bus_handle_t i2c_bus, i2c_port_t i2c_port);
    ~Ap0316AudioCodec() override;

    void EnableInput(bool enable) override;
    void EnableOutput(bool enable) override;
    void SetOutputVolume(int volume) override;
    bool InputData(std::vector<int16_t>& data) override;
    void OutputData(std::vector<int16_t>& data) override;

protected:
    int Read(int16_t* dest, int samples) override;
    int Write(const int16_t* data, int samples) override;

private:
    void CreateChannels();

    const audio_codec_data_if_t* data_if_ = nullptr;
    const audio_codec_ctrl_if_t* ctrl_if_ = nullptr;
    const audio_codec_gpio_if_t* gpio_if_ = nullptr;
    const audio_codec_if_t* codec_if_ = nullptr;
    esp_codec_dev_handle_t output_dev_ = nullptr;
    std::mutex input_mutex_;
    std::mutex output_mutex_;
    TickType_t last_input_error_time_ = 0;
    bool input_error_logged_ = false;
};

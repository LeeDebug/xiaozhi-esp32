#pragma once

#include "audio_codec.h"

#include <array>
#include <mutex>

#include "freertos/queue.h"
#include "usb/uac_host.h"

// Board-local adapter: Xiaozhi uses mono PCM; the USB sound card may use one or
// two channels. Both directions must provide 48 kHz, 16-bit PCM.
class UsbUacAudioCodec : public AudioCodec {
public:
    UsbUacAudioCodec();

    void Start() override;
    void EnableInput(bool enable) override;
    void EnableOutput(bool enable) override;
    void SetOutputVolume(int volume) override;
    bool InputData(std::vector<int16_t>& data) override;
    void OutputData(std::vector<int16_t>& data) override;

protected:
    int Read(int16_t* dest, int samples) override;
    int Write(const int16_t* data, int samples) override;

private:
    enum class EventType : uint8_t { ConnectInput, ConnectOutput, Disconnect, UpdateStreams };
    struct Event {
        EventType type;
        uint8_t addr;
        uint8_t iface_num;
        uac_host_device_handle_t handle;
    };
    struct Stream {
        uac_host_device_handle_t handle = nullptr;
        uint8_t channels = 0;
        uint8_t pending_addr = 0;
        uint8_t pending_iface_num = 0;
        bool pending = false;
        bool running = false;
    };

    static void HostTask(void* arg);
    static void ControlTask(void* arg);
    static void DriverCallback(uint8_t addr, uint8_t iface_num, uac_host_driver_event_t event,
                               void* arg);
    static void DeviceCallback(uac_host_device_handle_t handle, uac_host_device_event_t event,
                               void* arg);

    void OpenStream(bool input, uint8_t addr, uint8_t iface_num);
    void CloseStream(uac_host_device_handle_t handle);
    void UpdateStreams();
    void QueueUpdate();

    std::mutex mutex_;
    QueueHandle_t events_ = nullptr;
    Stream input_;
    Stream output_;
    // 10 ms of 48 kHz stereo PCM. Reused by the audio tasks, without allocations.
    std::array<int16_t, 960> pcm_buffer_{};
    bool started_ = false;
};

#include "usb_uac_audio_codec.h"

#include "herdsman_audio_meter.h"

#include <algorithm>
#include <chrono>
#include <cstddef>

#include "esp_log.h"
#include "usb/uac.h"
#include "usb/usb_host.h"

namespace {
constexpr char kTag[] = "WaveshareUac";
constexpr uint32_t kSampleRate = 48000;
constexpr int kFramesPerChunk = 480;
constexpr uint32_t kIoTimeoutMs = 100;
constexpr uint32_t kStreamBufferBytes = 16384;

void DescriptorClientCallback(const usb_host_client_event_msg_t*, void*) {}

bool IsUac1Device(uint8_t addr) {
    const usb_host_client_config_t client_config = {
        .is_synchronous = false,
        .max_num_event_msg = 2,
        .async = {.client_event_callback = DescriptorClientCallback, .callback_arg = nullptr},
    };
    usb_host_client_handle_t client = nullptr;
    esp_err_t err = usb_host_client_register(&client_config, &client);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "Cannot inspect USB audio device %u: %s", addr, esp_err_to_name(err));
        return false;
    }

    usb_device_handle_t device = nullptr;
    err = usb_host_device_open(client, addr, &device);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "Cannot open USB audio device %u for inspection: %s", addr,
                 esp_err_to_name(err));
        usb_host_client_deregister(client);
        return false;
    }

    const usb_config_desc_t* config = nullptr;
    err = usb_host_get_active_config_descriptor(device, &config);
    bool supported = false;
    bool found_header = false;
    if (err == ESP_OK && config != nullptr && config->bLength >= USB_CONFIG_DESC_SIZE) {
        const auto* bytes = reinterpret_cast<const uint8_t*>(config);
        const size_t length = config->wTotalLength;
        bool audio_control = false;
        for (size_t offset = config->bLength; offset + 2 <= length;) {
            const uint8_t desc_length = bytes[offset];
            const uint8_t desc_type = bytes[offset + 1];
            if (desc_length < 2 || desc_length > length - offset) {
                break;
            }
            if (desc_type == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
                audio_control = desc_length >= USB_INTF_DESC_SIZE &&
                                bytes[offset + 5] == USB_CLASS_AUDIO &&
                                bytes[offset + 6] == UAC_SUBCLASS_AUDIOCONTROL;
            } else if (audio_control && desc_type == UAC_CS_INTERFACE && desc_length >= 5 &&
                       bytes[offset + 2] == UAC_AC_HEADER) {
                found_header = true;
                const uint16_t version = bytes[offset + 3] | (uint16_t(bytes[offset + 4]) << 8);
                supported = version == 0x0100;
                if (!supported) {
                    ESP_LOGW(
                        kTag,
                        "USB audio device %u uses UAC version 0x%04x; host supports UAC 1.0 only",
                        addr, version);
                }
                break;
            }
            offset += desc_length;
        }
    }
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "Cannot read USB audio descriptor for device %u: %s", addr,
                 esp_err_to_name(err));
    } else if (!found_header) {
        ESP_LOGW(kTag, "USB audio device %u has no valid audio control header", addr);
    }
    usb_host_device_close(client, device);
    usb_host_client_deregister(client);
    return supported;
}
}  // namespace

UsbUacAudioCodec::UsbUacAudioCodec() {
    duplex_ = true;
    input_sample_rate_ = kSampleRate;
    output_sample_rate_ = kSampleRate;
    input_channels_ = 1;
    output_channels_ = 1;
}

void UsbUacAudioCodec::Start() {
    AudioCodec::Start();
    if (started_) {
        return;
    }

    events_ = xQueueCreate(16, sizeof(Event));
    if (events_ == nullptr) {
        ESP_LOGE(kTag, "Failed to allocate UAC event queue");
        return;
    }

    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    esp_err_t err = usb_host_install(&host_config);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "USB Host install failed: %s", esp_err_to_name(err));
        vQueueDelete(events_);
        events_ = nullptr;
        return;
    }

    if (xTaskCreate(HostTask, "uac_usb_host", 4096, this, 5, nullptr) != pdPASS) {
        ESP_LOGE(kTag, "Failed to create USB Host task");
        usb_host_uninstall();
        vQueueDelete(events_);
        events_ = nullptr;
        return;
    }
    started_ = true;

    const uac_host_driver_config_t driver_config = {
        .create_background_task = true,
        .task_priority = 5,
        .stack_size = 4096,
        .core_id = tskNO_AFFINITY,
        .callback = DriverCallback,
        .callback_arg = this,
    };
    err = uac_host_install(&driver_config);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "UAC driver install failed: %s", esp_err_to_name(err));
        return;
    }
    if (xTaskCreate(ControlTask, "uac_control", 4096, this, 5, nullptr) != pdPASS) {
        ESP_LOGE(kTag, "Failed to create UAC control task");
        uac_host_uninstall();
        return;
    }
    ESP_LOGI(kTag, "USB UAC Host ready; waiting for 48 kHz, 16-bit PCM device");
}

void UsbUacAudioCodec::HostTask(void* arg) {
    (void)arg;
    while (true) {
        uint32_t flags = 0;
        esp_err_t err = usb_host_lib_handle_events(portMAX_DELAY, &flags);
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "USB Host event error: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            usb_host_device_free_all();
        }
    }
}

void UsbUacAudioCodec::DriverCallback(uint8_t addr, uint8_t iface_num,
                                      uac_host_driver_event_t event, void* arg) {
    auto* self = static_cast<UsbUacAudioCodec*>(arg);
    const Event item = {
        event == UAC_HOST_DRIVER_EVENT_RX_CONNECTED ? EventType::ConnectInput
                                                    : EventType::ConnectOutput,
        addr,
        iface_num,
        nullptr,
    };
    if (xQueueSend(self->events_, &item, 0) != pdTRUE) {
        ESP_LOGW(kTag, "UAC connect event queue full");
    }
}

void UsbUacAudioCodec::DeviceCallback(uac_host_device_handle_t handle,
                                      uac_host_device_event_t event, void* arg) {
    if (event != UAC_HOST_DRIVER_EVENT_DISCONNECTED) {
        return;
    }
    auto* self = static_cast<UsbUacAudioCodec*>(arg);
    const Event item = {EventType::Disconnect, 0, 0, handle};
    if (xQueueSend(self->events_, &item, 0) != pdTRUE) {
        ESP_LOGE(kTag, "UAC disconnect event queue full");
    }
}

void UsbUacAudioCodec::ControlTask(void* arg) {
    auto* self = static_cast<UsbUacAudioCodec*>(arg);
    Event item;
    while (xQueueReceive(self->events_, &item, portMAX_DELAY) == pdTRUE) {
        switch (item.type) {
            case EventType::ConnectInput:
                self->OpenStream(true, item.addr, item.iface_num);
                break;
            case EventType::ConnectOutput:
                self->OpenStream(false, item.addr, item.iface_num);
                break;
            case EventType::Disconnect:
                self->CloseStream(item.handle);
                break;
            case EventType::UpdateStreams: {
                std::lock_guard<std::mutex> lock(self->mutex_);
                self->UpdateStreams();
            } break;
        }
    }
}

void UsbUacAudioCodec::OpenStream(bool input, uint8_t addr, uint8_t iface_num) {
    std::lock_guard<std::mutex> lock(mutex_);
    Stream& stream = input ? input_ : output_;
    if (stream.handle != nullptr) {
        stream.pending_addr = addr;
        stream.pending_iface_num = iface_num;
        stream.pending = true;
        return;
    }

    // usb_host_uac 1.x rejects UAC 2.0 and its error path dereferences a null
    // interface. Inspect the cached descriptor before entering that path.
    if (!IsUac1Device(addr)) {
        return;
    }

    const uac_host_device_config_t device_config = {
        .addr = addr,
        .iface_num = iface_num,
        .buffer_size = kStreamBufferBytes,
        .buffer_threshold = kFramesPerChunk * sizeof(int16_t),
        .callback = DeviceCallback,
        .callback_arg = this,
    };
    uac_host_device_handle_t handle = nullptr;
    esp_err_t err = uac_host_device_open(&device_config, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Open UAC %s failed: %s", input ? "mic" : "speaker", esp_err_to_name(err));
        return;
    }

    uac_host_dev_info_t info = {};
    err = uac_host_get_device_info(handle, &info);
    uint8_t channels = 0;
    if (err == ESP_OK) {
        // Inspect every alternate setting, even after finding a matching mode.
        // usb_host_uac 1.x retains the advertised count after truncating its rate
        // array and uses that count in device_start(). Do not enter that path
        // with any truncated alternate setting.
        for (int alt_index = 1; alt_index <= info.iface_alt_num; ++alt_index) {
            uac_host_dev_alt_param_t alt = {};
            err = uac_host_get_device_alt_param(handle, alt_index, &alt);
            if (err != ESP_OK) {
                ESP_LOGE(kTag, "Cannot inspect UAC %s alt %d: %s", input ? "mic" : "speaker",
                         alt_index, esp_err_to_name(err));
                uac_host_device_close(handle);
                return;
            }
            const size_t rate_capacity = sizeof(alt.sample_freq) / sizeof(alt.sample_freq[0]);
            if (alt.sample_freq_type > rate_capacity) {
                ESP_LOGE(kTag,
                         "UAC %s alt %d advertises %u rates, host stores only %u; "
                         "set CONFIG_UAC_FREQ_NUM_MAX >= %u and reconfigure",
                         input ? "mic" : "speaker", alt_index, alt.sample_freq_type,
                         static_cast<unsigned>(rate_capacity), alt.sample_freq_type);
                uac_host_device_close(handle);
                return;
            }
            bool rate_supported = alt.sample_freq_type == 0 &&
                                  alt.sample_freq_lower <= kSampleRate &&
                                  kSampleRate <= alt.sample_freq_upper;
            for (size_t i = 0; !rate_supported && i < alt.sample_freq_type; ++i) {
                rate_supported = alt.sample_freq[i] == kSampleRate;
            }
            ESP_LOGI(kTag, "UAC %s alt %d: format=%u, %u ch, %u-bit/%u-byte, 48 kHz=%s",
                     input ? "mic" : "speaker", alt_index, alt.format, alt.channels,
                     alt.bit_resolution, alt.subframe_size, rate_supported ? "yes" : "no");
            if (alt.format == 1 && alt.bit_resolution == 16 && alt.subframe_size == 2 &&
                (alt.channels == 1 || alt.channels == 2) && rate_supported &&
                (channels == 0 || alt.channels < channels)) {
                channels = alt.channels;
            }
        }
    } else {
        ESP_LOGE(kTag, "Cannot inspect UAC %s: %s", input ? "mic" : "speaker",
                 esp_err_to_name(err));
        uac_host_device_close(handle);
        return;
    }
    if (channels == 0) {
        ESP_LOGE(kTag, "UAC %s has no 48 kHz/16-bit mono or stereo PCM mode",
                 input ? "mic" : "speaker");
        uac_host_device_close(handle);
        return;
    }

    const uac_host_stream_config_t stream_config = {
        .channels = channels,
        .bit_resolution = 16,
        .sample_freq = kSampleRate,
        .flags = FLAG_STREAM_SUSPEND_AFTER_START,
    };
    err = uac_host_device_start(handle, &stream_config);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Start UAC %s failed: %s", input ? "mic" : "speaker", esp_err_to_name(err));
        uac_host_device_close(handle);
        return;
    }
    stream.handle = handle;
    stream.channels = channels;
    ESP_LOGI(kTag, "UAC %s connected: %u channel(s), 48 kHz/16-bit", input ? "mic" : "speaker",
             channels);
    UpdateStreams();
}

void UsbUacAudioCodec::CloseStream(uac_host_device_handle_t handle) {
    std::unique_lock<std::mutex> lock(mutex_);
    Stream* stream = input_.handle == handle    ? &input_
                     : output_.handle == handle ? &output_
                                                : nullptr;
    if (stream == nullptr) {
        return;
    }
    const bool input = stream == &input_;
    const bool pending = stream->pending;
    const uint8_t pending_addr = stream->pending_addr;
    const uint8_t pending_iface_num = stream->pending_iface_num;
    stream->handle = nullptr;
    stream->channels = 0;
    stream->pending = false;
    stream->running = false;
    stream_cv_.notify_all();
    uac_host_device_close(handle);
    ESP_LOGI(kTag, "UAC interface disconnected");
    lock.unlock();
    if (pending) {
        OpenStream(input, pending_addr, pending_iface_num);
    }
}

void UsbUacAudioCodec::UpdateStreams() {
    // The caller holds mutex_ while a stream is resumed, suspended or closed.
    auto update = [](Stream& stream, bool enable) {
        if (stream.handle == nullptr || stream.running == enable) {
            return;
        }
        esp_err_t err =
            enable ? uac_host_device_resume(stream.handle) : uac_host_device_suspend(stream.handle);
        if (err == ESP_OK) {
            stream.running = enable;
        } else {
            ESP_LOGW(kTag, "UAC stream %s failed: %s", enable ? "resume" : "suspend",
                     esp_err_to_name(err));
        }
    };
    update(input_, input_enabled_);
    update(output_, output_enabled_);
    stream_cv_.notify_all();
}

void UsbUacAudioCodec::QueueUpdate() {
    if (events_ != nullptr) {
        const Event item = {EventType::UpdateStreams, 0, 0, nullptr};
        xQueueSend(events_, &item, 0);
    }
}

void UsbUacAudioCodec::EnableInput(bool enable) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (input_enabled_ == enable) {
        return;
    }
    AudioCodec::EnableInput(enable);
    QueueUpdate();
}

void UsbUacAudioCodec::EnableOutput(bool enable) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (output_enabled_ == enable) {
        return;
    }
    AudioCodec::EnableOutput(enable);
    QueueUpdate();
}

void UsbUacAudioCodec::SetOutputVolume(int volume) {
    std::lock_guard<std::mutex> lock(mutex_);
    AudioCodec::SetOutputVolume(std::clamp(volume, 0, 100));
}

int UsbUacAudioCodec::Read(int16_t* dest, int samples) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!input_.running || input_.handle == nullptr) {
        return 0;
    }
    int completed = 0;
    while (completed < samples) {
        const int frames = std::min(samples - completed, kFramesPerChunk);
        uint8_t* target = input_.channels == 1 ? reinterpret_cast<uint8_t*>(dest + completed)
                                               : reinterpret_cast<uint8_t*>(pcm_buffer_.data());
        const uint32_t bytes_needed = frames * input_.channels * sizeof(int16_t);
        uint32_t received = 0;
        const TickType_t begin = xTaskGetTickCount();
        const TickType_t timeout = pdMS_TO_TICKS(kIoTimeoutMs);
        while (received < bytes_needed) {
            const TickType_t elapsed = xTaskGetTickCount() - begin;
            if (elapsed >= timeout) {
                return 0;
            }
            uint32_t bytes_read = 0;
            esp_err_t err =
                uac_host_device_read(input_.handle, target + received, bytes_needed - received,
                                     &bytes_read, timeout - elapsed);
            if (err != ESP_OK || bytes_read == 0 || bytes_read > bytes_needed - received) {
                return 0;
            }
            received += bytes_read;
        }
        if (input_.channels == 2) {
            for (int i = 0; i < frames; ++i) {
                dest[completed + i] = (static_cast<int32_t>(pcm_buffer_[2 * i]) +
                                       static_cast<int32_t>(pcm_buffer_[2 * i + 1])) /
                                      2;
            }
        }
        completed += frames;
    }
    return completed;
}

int UsbUacAudioCodec::Write(const int16_t* data, int samples) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!output_enabled_ || output_.handle == nullptr) {
        return 0;
    }
    // EnableOutput schedules USB control work. Let that task resume the stream
    // before consuming the first playback buffer, including after idle suspend.
    // Waiting releases mutex_ so the control task can make progress.
    if (!stream_cv_.wait_for(lock, std::chrono::milliseconds(kIoTimeoutMs), [this]() {
            return output_.running || !output_enabled_ || output_.handle == nullptr;
        })) {
        ESP_LOGW(kTag, "UAC speaker resume timed out before playback");
        return 0;
    }
    if (!output_enabled_ || !output_.running || output_.handle == nullptr) {
        return 0;
    }
    const int volume = std::clamp(output_volume_, 0, 100);
    const int gain = volume * volume;
    int completed = 0;
    while (completed < samples) {
        const int frames = std::min(samples - completed, kFramesPerChunk);
        for (int i = 0; i < frames; ++i) {
            const int16_t sample = static_cast<int32_t>(data[completed + i]) * gain / 10000;
            pcm_buffer_[i * output_.channels] = sample;
            if (output_.channels == 2) {
                pcm_buffer_[i * 2 + 1] = sample;
            }
        }
        esp_err_t err = uac_host_device_write(
            output_.handle, reinterpret_cast<uint8_t*>(pcm_buffer_.data()),
            frames * output_.channels * sizeof(int16_t), pdMS_TO_TICKS(kIoTimeoutMs));
        if (err != ESP_OK) {
            ESP_LOGW(kTag, "UAC speaker write failed: %s", esp_err_to_name(err));
            return completed;
        }
        completed += frames;
    }
    return completed;
}

bool UsbUacAudioCodec::InputData(std::vector<int16_t>& data) {
    const bool success = AudioCodec::InputData(data);
    if (success) {
        HerdsmanAudioMeter::Publish(data, input_channels(), false);
    }
    return success;
}

void UsbUacAudioCodec::OutputData(std::vector<int16_t>& data) {
    AudioCodec::OutputData(data);
    HerdsmanAudioMeter::Publish(data, output_channels(), true);
}

#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <material_symbols.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>

#include "application.h"
#include "assets/lang_config.h"
#include "audio_codec.h"
#include "board.h"
#include "dynamic_glyph_cache.h"
#include "jpg/image_to_jpeg.h"
#include "jpg/jpeg_to_image.h"
#include "lvgl_display.h"
#include "lvgl_theme.h"
#include "settings.h"
#include "src/draw/lv_image_decoder_private.h"
#include "src/misc/cache/instance/lv_image_cache.h"

#define TAG "Display"

namespace {
enum class PopupImageFormat { kUnsupported, kPng, kJpeg };

PopupImageFormat GetPopupImageFormat(const uint8_t* data, size_t size) {
    if (data == nullptr || size < 4) {
        return PopupImageFormat::kUnsupported;
    }
    [[maybe_unused]] auto valid_dimensions = [](uint32_t width, uint32_t height) {
        return width > 0 && height > 0 && width <= 2048 && height <= 2048 &&
               width * height <= 1024 * 1024;
    };
#if LV_USE_LODEPNG
    constexpr uint8_t png_signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
    constexpr uint8_t png_ihdr[] = {0, 0, 0, 13, 'I', 'H', 'D', 'R'};
    if (size >= 33 && std::memcmp(data, png_signature, sizeof(png_signature)) == 0 &&
        std::memcmp(data + sizeof(png_signature), png_ihdr, sizeof(png_ihdr)) == 0) {
        auto read_be32 = [](const uint8_t* value) -> uint32_t {
            return (uint32_t(value[0]) << 24) | (uint32_t(value[1]) << 16) |
                   (uint32_t(value[2]) << 8) | value[3];
        };
        if (valid_dimensions(read_be32(data + 16), read_be32(data + 20))) {
            return PopupImageFormat::kPng;
        }
    }
#endif
#ifndef CONFIG_IDF_TARGET_ESP32
    if (data[0] == 0xff && data[1] == 0xd8) {
        // The software JPEG decoder accepts baseline frames. Bound the pixel buffer
        // before decoding by checking the dimensions in the JPEG frame header.
        size_t offset = 2;
        while (offset + 4 <= size && data[offset++] == 0xff) {
            while (offset < size && data[offset] == 0xff) {
                ++offset;
            }
            if (offset + 3 > size) {
                break;
            }
            const uint8_t marker = data[offset++];
            const size_t length = (size_t(data[offset]) << 8) | data[offset + 1];
            if (marker == 0xda || marker == 0xd9 || length < 2 || length > size - offset) {
                break;
            }
            if (marker == 0xc0 && length >= 8) {
                const auto height = (uint32_t(data[offset + 3]) << 8) | data[offset + 4];
                const auto width = (uint32_t(data[offset + 5]) << 8) | data[offset + 6];
                if (valid_dimensions(width, height)) {
                    return PopupImageFormat::kJpeg;
                }
                break;
            }
            offset += length;
        }
    }
#endif
    return PopupImageFormat::kUnsupported;
}
}  // namespace

LvglDisplay::LvglDisplay() {
    dynamic_glyph_cache_ = std::make_unique<DynamicGlyphCache>();
    // Notification timer
    esp_timer_create_args_t notification_timer_args = {
        .callback =
            [](void* arg) {
                LvglDisplay* display = static_cast<LvglDisplay*>(arg);
                DisplayLockGuard lock(display);
                lv_obj_add_flag(display->notification_label_, LV_OBJ_FLAG_HIDDEN);
                lv_obj_remove_flag(display->status_label_, LV_OBJ_FLAG_HIDDEN);
            },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "notification_timer",
        .skip_unhandled_events = false,
    };
    ESP_ERROR_CHECK(esp_timer_create(&notification_timer_args, &notification_timer_));

    // Create a power management lock
    auto ret = esp_pm_lock_create(ESP_PM_APB_FREQ_MAX, 0, "display_update", &pm_lock_);
    if (ret == ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGI(TAG, "Power management not supported");
    } else {
        ESP_ERROR_CHECK(ret);
    }
}

bool LvglDisplay::SetTextFont(std::shared_ptr<LvglFont> text_font) {
    if (text_font == nullptr || text_font->font() == nullptr) {
        return false;
    }

    DisplayLockGuard lock(this);
    auto& theme_manager = LvglThemeManager::GetInstance();
    auto light_theme = theme_manager.GetTheme("light");
    auto dark_theme = theme_manager.GetTheme("dark");
    if (light_theme == nullptr && dark_theme == nullptr) {
        return false;
    }

    // LVGL styles keep raw lv_font_t pointers. Keep the previous font owners alive until the
    // current theme has rebound every style to the new font.
    auto previous_light_font = light_theme != nullptr ? light_theme->text_font() : nullptr;
    auto previous_dark_font = dark_theme != nullptr ? dark_theme->text_font() : nullptr;
    if (light_theme != nullptr) {
        light_theme->set_text_font(text_font);
    }
    if (dark_theme != nullptr) {
        dark_theme->set_text_font(text_font);
    }
    if (current_theme_ != nullptr) {
        SetTheme(current_theme_);
    }
    previous_light_font.reset();
    previous_dark_font.reset();
    return true;
}

bool LvglDisplay::AddTextGlyphs(const std::vector<TextGlyph>& glyphs, uint8_t bpp) {
    if (dynamic_glyph_cache_ == nullptr) {
        return false;
    }
    if (glyphs.empty()) {
        if (!TextGlyphStorageUsesPsram()) {
            ClearTextGlyphs();
        }
        return false;
    }
    if (bpp != 1 && bpp != 4) {
        return false;
    }

    DisplayLockGuard lock(this);
    if (!lock || current_theme_ == nullptr) {
        return false;
    }
    auto text_font = current_theme_->GetTextFont();
    if (text_font == nullptr) {
        return false;
    }

    auto fallback = dynamic_glyph_cache_->EnsureFont(text_font->font(), bpp);
    if (fallback == nullptr) {
        return false;
    }
    text_font->SetFallback(fallback);
    return dynamic_glyph_cache_->AddGlyphs(glyphs);
}

void LvglDisplay::ClearTextGlyphs() {
    if (dynamic_glyph_cache_ == nullptr) {
        return;
    }
    DisplayLockGuard lock(this);
    dynamic_glyph_cache_->Clear();
}

LvglDisplay::~LvglDisplay() {
    if (image_popup_ != nullptr) {
        lv_obj_delete(image_popup_);
    }
    if (notification_timer_ != nullptr) {
        esp_timer_stop(notification_timer_);
        esp_timer_delete(notification_timer_);
    }

    if (network_label_ != nullptr) {
        lv_obj_del(network_label_);
    }
    if (notification_label_ != nullptr) {
        lv_obj_del(notification_label_);
    }
    if (status_label_ != nullptr) {
        lv_obj_del(status_label_);
    }
    if (mute_label_ != nullptr) {
        lv_obj_del(mute_label_);
    }
    if (battery_label_ != nullptr) {
        lv_obj_del(battery_label_);
    }
    if (low_battery_popup_ != nullptr) {
        lv_obj_del(low_battery_popup_);
    }
    if (pm_lock_ != nullptr) {
        esp_pm_lock_delete(pm_lock_);
    }
}

void LvglDisplay::SetStatus(const char* status) {
    if (!setup_ui_called_) {
        ESP_LOGW(TAG, "SetStatus('%s') called before SetupUI() - message will be lost!", status);
    }
    DisplayLockGuard lock(this);
    if (status_label_ == nullptr) {
        if (setup_ui_called_) {
            ESP_LOGW(TAG,
                     "SetStatus('%s') failed: status_label_ is nullptr (SetupUI() was called but "
                     "label not created)",
                     status);
        }
        return;
    }
    lv_label_set_text(status_label_, status);
    lv_obj_remove_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    last_status_update_time_ = std::chrono::system_clock::now();
}

void LvglDisplay::ShowNotification(const std::string& notification, int duration_ms) {
    ShowNotification(notification.c_str(), duration_ms);
}

void LvglDisplay::ShowNotification(const char* notification, int duration_ms) {
    if (!setup_ui_called_) {
        ESP_LOGW(TAG, "ShowNotification('%s') called before SetupUI() - message will be lost!",
                 notification);
    }
    DisplayLockGuard lock(this);
    if (notification_label_ == nullptr) {
        if (setup_ui_called_) {
            ESP_LOGW(TAG,
                     "ShowNotification('%s') failed: notification_label_ is nullptr (SetupUI() was "
                     "called but label not created)",
                     notification);
        }
        return;
    }
    lv_label_set_text(notification_label_, notification);
    lv_obj_remove_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(status_label_, LV_OBJ_FLAG_HIDDEN);

    esp_timer_stop(notification_timer_);
    ESP_ERROR_CHECK(esp_timer_start_once(notification_timer_, duration_ms * 1000));
}

void LvglDisplay::UpdateStatusBar(bool update_all) {
    auto& app = Application::GetInstance();
    auto& board = Board::GetInstance();
    auto codec = board.GetAudioCodec();

    // Update mute icon
    {
        DisplayLockGuard lock(this);
        if (mute_label_ == nullptr) {
            return;
        }

        // Update icon if mute state changes
        if (codec->output_volume() == 0 && !muted_) {
            muted_ = true;
            lv_label_set_text(mute_label_, MATERIAL_SYMBOLS_VOLUME_OFF);
        } else if (codec->output_volume() > 0 && muted_) {
            muted_ = false;
            lv_label_set_text(mute_label_, "");
        }
    }

    // Update time — show HH:MM when idle; refresh immediately on state
    // transitions (update_all=true) and on every minute change thereafter.
    if (app.GetDeviceState() == kDeviceStateIdle) {
        time_t now = time(NULL);
        struct tm* tm_now = localtime(&now);
        if (tm_now->tm_year >= 2025 - 1900) {
            int cur_min = tm_now->tm_hour * 60 + tm_now->tm_min;
            if (update_all || cur_min != last_displayed_clock_min_) {
                last_displayed_clock_min_ = cur_min;
                char time_str[16];
                strftime(time_str, sizeof(time_str), "%H:%M", tm_now);
                SetStatus(time_str);
            }
        } else {
            ESP_LOGW(TAG, "System time not set (tm_year=%d)", tm_now->tm_year);
        }
    } else {
        // Reset so the clock re-appears immediately when idle resumes.
        last_displayed_clock_min_ = -1;
    }

    esp_pm_lock_acquire(pm_lock_);
    // Update battery icon
    int battery_level;
    bool charging, discharging;
    const char* icon = nullptr;
    if (board.GetBatteryLevel(battery_level, charging, discharging)) {
        if (charging) {
            icon = MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_BOLT;
        } else {
            const char* levels[] = {
                MATERIAL_SYMBOLS_BATTERY_ANDROID_0,
                MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_1,
                MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_2,
                MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_3,
                MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_4,
                MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_5,
                MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_6,
                MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_FULL,
            };
            int level_index = battery_level <= 0
                                  ? 0
                                  : (battery_level >= 100 ? 7 : 1 + ((battery_level - 1) * 6 / 99));
            icon = levels[level_index];
        }
        DisplayLockGuard lock(this);
        if (battery_label_ != nullptr && battery_icon_ != icon) {
            battery_icon_ = icon;
            lv_label_set_text(battery_label_, battery_icon_);
        }

        // Check low battery popup only when clock tick event is triggered
        // Because when initializing, the battery level is not ready yet.
        if (low_battery_popup_ != nullptr && !update_all) {
            if (strcmp(icon, MATERIAL_SYMBOLS_BATTERY_ANDROID_0) == 0 && discharging) {
                if (lv_obj_has_flag(low_battery_popup_,
                                    LV_OBJ_FLAG_HIDDEN)) {  // Show if low battery popup is hidden
                    lv_obj_remove_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);
                    app.Schedule([&app]() { app.PlaySound(Lang::Sounds::OGG_LOW_BATTERY); });
                }
            } else {
                // Hide the low battery popup when the battery is not empty
                if (!lv_obj_has_flag(low_battery_popup_,
                                     LV_OBJ_FLAG_HIDDEN)) {  // Hide if low battery popup is shown
                    lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);
                }
            }
        }
    }

    // Update network icon every 10 seconds
    static int seconds_counter = 0;
    if (update_all || seconds_counter++ % 10 == 0) {
        // Don't read 4G network status during firmware upgrade to avoid occupying UART resources
        auto device_state = Application::GetInstance().GetDeviceState();
        static const std::vector<DeviceState> allowed_states = {
            kDeviceStateIdle,      kDeviceStateStarting,   kDeviceStateWifiConfiguring,
            kDeviceStateListening, kDeviceStateActivating,
        };
        if (std::find(allowed_states.begin(), allowed_states.end(), device_state) !=
            allowed_states.end()) {
            icon = board.GetNetworkStateIcon();
            if (network_label_ != nullptr && icon != nullptr && network_icon_ != icon) {
                DisplayLockGuard lock(this);
                network_icon_ = icon;
                lv_label_set_text(network_label_, network_icon_);
            }
        }
    }

    esp_pm_lock_release(pm_lock_);
}

bool LvglDisplay::SupportsImagePopup() const {
#if LV_USE_LODEPNG || !defined(CONFIG_IDF_TARGET_ESP32)
    return display_ != nullptr && !IsMonochrome();
#else
    return false;
#endif
}

const char* LvglDisplay::ShowImagePopup(ImageBuffer data, size_t size) {
    if (!data) {
        return "Image data is unavailable";
    }
    const auto* bytes = static_cast<const uint8_t*>(data.get());
    const auto format = GetPopupImageFormat(bytes, size);
    if (format == PopupImageFormat::kUnsupported) {
        return "Unsupported image format or dimensions exceed the limit";
    }

    std::unique_ptr<LvglAllocatedImage> image;
#ifndef CONFIG_IDF_TARGET_ESP32
    if (format == PopupImageFormat::kJpeg) {
        uint8_t* pixels = nullptr;
        size_t pixel_size = 0, width = 0, height = 0, stride = 0;
        if (jpeg_to_image(bytes, size, &pixels, &pixel_size, &width, &height, &stride) != ESP_OK) {
            return "Failed to decode JPEG";
        }
        ImageBuffer decoded(pixels, heap_caps_free);
        image = std::make_unique<LvglAllocatedImage>(decoded.get(), pixel_size, width, height,
                                                     stride, LV_COLOR_FORMAT_RGB565);
        decoded.release();
    }
#endif

    DisplayLockGuard lock(this);
    if (!lock || display_ == nullptr) {
        return "Screen is unavailable";
    }
    std::unique_ptr<lv_image_decoder_dsc_t> png;
    const lv_image_dsc_t* descriptor = nullptr;
    const void* source = nullptr;
    if (format == PopupImageFormat::kPng) {
        image = std::make_unique<LvglAllocatedImage>(data.get(), size);
        data.release();
        if (!image->IsValid()) {
            return "Invalid PNG header";
        }
        // Hold the decoded draw buffer directly so its lifetime does not depend on cache size.
        png = std::make_unique<lv_image_decoder_dsc_t>();
        lv_image_decoder_args_t args{};
        args.no_cache = true;
        args.stride_align = LV_DRAW_BUF_STRIDE_ALIGN != 1;
        if (lv_image_decoder_open(png.get(), image->image_dsc(), &args) != LV_RESULT_OK) {
            lv_image_cache_drop(image->image_dsc());
            return "Failed to decode PNG";
        }
        if (png->decoded == nullptr) {
            lv_image_decoder_close(png.get());
            lv_image_cache_drop(image->image_dsc());
            return "PNG decoder produced no image";
        }
        descriptor = image->image_dsc();
        source = png->decoded;
    } else {
        descriptor = image->image_dsc();
        source = descriptor;
    }

    const int screen_width = lv_display_get_horizontal_resolution(display_);
    const int screen_height = lv_display_get_vertical_resolution(display_);
    if (image_popup_ == nullptr) {
        image_popup_ = lv_obj_create(lv_display_get_layer_top(display_));
        lv_obj_remove_style_all(image_popup_);
        lv_obj_set_pos(image_popup_, 0, 0);
        lv_obj_set_style_bg_color(image_popup_, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(image_popup_, LV_OPA_COVER, 0);
        lv_obj_remove_flag(image_popup_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(image_popup_, LV_OBJ_FLAG_CLICKABLE);
        image_popup_picture_ = lv_image_create(image_popup_);
        image_popup_close_ = lv_button_create(image_popup_);
        auto* label = lv_label_create(image_popup_close_);
        lv_label_set_text(label, LV_SYMBOL_CLOSE);
        lv_obj_center(label);
        lv_obj_add_event_cb(
            image_popup_close_,
            [](lv_event_t* event) {
                auto* display = static_cast<LvglDisplay*>(lv_event_get_user_data(event));
                // The input event already holds the display lock. Keep the widgets alive.
                display->ClearImagePopupLocked();
            },
            LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(
            image_popup_,
            [](lv_event_t* event) {
                auto* display = static_cast<LvglDisplay*>(lv_event_get_user_data(event));
                display->ClearImagePopupLocked();
                display->image_popup_ = nullptr;
                display->image_popup_picture_ = nullptr;
                display->image_popup_close_ = nullptr;
            },
            LV_EVENT_DELETE, this);
    }
    ClearImagePopupLocked();
    image_popup_image_ = std::move(image);
    image_popup_png_ = std::move(png);
    lv_obj_set_size(image_popup_, screen_width, screen_height);
    lv_image_set_src(image_popup_picture_, source);
    // Cover the screen while preserving aspect ratio, rounding up to avoid edge gaps.
    const int scale =
        std::max((screen_width * 256 + descriptor->header.w - 1) / descriptor->header.w,
                 (screen_height * 256 + descriptor->header.h - 1) / descriptor->header.h);
    lv_image_set_scale(image_popup_picture_, std::max(1, scale));
    lv_obj_center(image_popup_picture_);
    const int button_size = std::min(40, std::min(screen_width, screen_height));
    lv_obj_set_size(image_popup_close_, button_size, button_size);
    lv_obj_align(image_popup_close_, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_move_foreground(image_popup_close_);
    lv_obj_move_foreground(image_popup_);
    lv_obj_remove_flag(image_popup_, LV_OBJ_FLAG_HIDDEN);
    return nullptr;
}

void LvglDisplay::ClearImagePopupLocked() {
    lv_obj_add_flag(image_popup_, LV_OBJ_FLAG_HIDDEN);
    if (!image_popup_image_) {
        return;
    }
    const auto* source = lv_image_get_src(image_popup_picture_);
    lv_image_set_src(image_popup_picture_, nullptr);
    if (source != nullptr) {
        lv_image_cache_drop(source);
    }
    if (image_popup_png_ != nullptr) {
        lv_image_decoder_close(image_popup_png_.get());
        image_popup_png_.reset();
        lv_image_cache_drop(image_popup_image_->image_dsc());
    }
    image_popup_image_.reset();
}

void LvglDisplay::SetPreviewImage(std::unique_ptr<LvglImage> image) {}

void LvglDisplay::SetPowerSaveMode(bool on) {
    if (on) {
        SetChatMessage("system", "");
        SetEmotion("sleepy");
    } else {
        SetChatMessage("system", "");
        SetEmotion("neutral");
    }
}

bool LvglDisplay::SnapshotToJpeg(std::string& jpeg_data, int quality) {
#if CONFIG_LV_USE_SNAPSHOT
    DisplayLockGuard lock(this);

    lv_obj_t* screen = lv_screen_active();
    lv_draw_buf_t* draw_buffer = lv_snapshot_take(screen, LV_COLOR_FORMAT_RGB565);
    if (draw_buffer == nullptr) {
        ESP_LOGE(TAG, "Failed to take snapshot, draw_buffer is nullptr");
        return false;
    }

    // swap bytes
    uint16_t* data = (uint16_t*)draw_buffer->data;
    size_t pixel_count = draw_buffer->data_size / 2;
    for (size_t i = 0; i < pixel_count; i++) {
        data[i] = __builtin_bswap16(data[i]);
    }

    // Clear output string and use callback version to avoid pre-allocating large memory blocks
    jpeg_data.clear();

    // Use callback-based JPEG encoder to further save memory
    bool ret =
        image_to_jpeg_cb((uint8_t*)draw_buffer->data, draw_buffer->data_size, draw_buffer->header.w,
                         draw_buffer->header.h, V4L2_PIX_FMT_RGB565, quality,
                         [](void* arg, size_t index, const void* data, size_t len) -> size_t {
                             std::string* output = static_cast<std::string*>(arg);
                             if (data && len > 0) {
                                 output->append(static_cast<const char*>(data), len);
                             }
                             return len;
                         },
                         &jpeg_data);
    if (!ret) {
        ESP_LOGE(TAG, "Failed to convert image to JPEG");
    }

    lv_draw_buf_destroy(draw_buffer);
    return ret;
#else
    ESP_LOGE(TAG, "LV_USE_SNAPSHOT is not enabled");
    return false;
#endif
}

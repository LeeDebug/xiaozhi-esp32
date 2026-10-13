#pragma once
/**
 * Board-local dashboard: top logo/settings, left audio meter/chat control,
 * central conversation and right production trends with independent scales.
 */

#include "display/lcd_display.h"

class HerdsmanLcdDisplay : public MipiLcdDisplay {
public:
    HerdsmanLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                       int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                       bool swap_xy);

    ~HerdsmanLcdDisplay() override;
    void SetupUI() override;
    void SetTheme(Theme* theme) override;
    void SetStatus(const char* status) override;
    void UpdateStatusBar(bool update_all = false) override;
    void SetChatMessage(const char* role, const char* content) override;
    void ClearChatMessages() override;
    void SetEmotion(const char* emotion) override;

private:
    static constexpr int kTopBarHeight = 72;
    static constexpr int kReferenceCenterWidth = 1000;
    static constexpr int kMaxChatBubbleWidth = 780;
    static constexpr int kMaxSystemBubbleWidth = 850;

    int side_column_width_ = 460;
    int right_column_width_ = 580;
    int content_width_ = kReferenceCenterWidth;
    int chat_bubble_width_ = kMaxChatBubbleWidth;
    int system_bubble_width_ = kMaxSystemBubbleWidth;

    static constexpr int kAudioBarCount = 33;
    lv_obj_t* audio_canvas_ = nullptr;
    lv_draw_buf_t audio_canvas_draw_buf_ = {};
    uint8_t* audio_canvas_buffer_ = nullptr;
    int audio_canvas_width_ = 0;
    int audio_canvas_height_ = 0;
    int audio_canvas_last_scale_ = -1;
    lv_timer_t* audio_meter_timer_ = nullptr;
    int audio_meter_height_ = 160;
    int audio_level_ = 0;

    void CreateAudioPanel(lv_obj_t* parent);
    void CreateProductionChart(lv_obj_t* parent);
    void UpdateAudioMeter();
    void DrawAudioMeter(int scale);

    lv_obj_t* chat_list_ = nullptr;
    lv_obj_t* standby_panel_ = nullptr;
    lv_obj_t* settings_panel_ = nullptr;
    lv_obj_t* settings_time_label_ = nullptr;
    lv_obj_t* signal_value_label_ = nullptr;
    lv_obj_t* date_time_label_ = nullptr;
    lv_obj_t* settings_button_label_ = nullptr;
    lv_obj_t* settings_button_icon_ = nullptr;
    lv_obj_t* chat_button_label_ = nullptr;
    lv_obj_t* chat_button_icon_ = nullptr;
    lv_obj_t* brightness_slider_ = nullptr;
    lv_obj_t* volume_slider_ = nullptr;
    lv_obj_t* brightness_value_label_ = nullptr;
    lv_obj_t* volume_value_label_ = nullptr;

    void CreateTopBar(lv_obj_t* parent);
    void CreateHomeContent(lv_obj_t* parent);
    void CreateStandbyPanel();
    void CreateSettingsPanel(lv_obj_t* parent);
    void UpdateColumnWidths();
    void UpdateHomeState();
    void UpdateDateTime();
    void UpdateSignalStrength();
    void UpdateSettingsTime();
    void UpdateSettingsValues();
    void ShowSettings(bool show);

    static void SettingsButtonEvent(lv_event_t* event);
    static void BackButtonEvent(lv_event_t* event);
    static void ChatButtonEvent(lv_event_t* event);
    static void BrightnessSliderEvent(lv_event_t* event);
    static void VolumeSliderEvent(lv_event_t* event);
};

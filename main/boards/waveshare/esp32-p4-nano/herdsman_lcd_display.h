#pragma once

#include "display/lcd_display.h"

class HerdsmanLcdDisplay : public MipiLcdDisplay {
public:
    HerdsmanLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                       int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                       bool swap_xy);

    void SetupUI() override;
    void SetTheme(Theme* theme) override;
    void SetStatus(const char* status) override;
    void UpdateStatusBar(bool update_all = false) override;
    void SetChatMessage(const char* role, const char* content) override;
    void ClearChatMessages() override;
    void SetEmotion(const char* emotion) override;

private:
    static constexpr int kTopBarHeight = 72;
    static constexpr int kSideWidth = 460;
    static constexpr int kChatBubbleWidth = 780;
    static constexpr int kSystemBubbleWidth = 850;

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

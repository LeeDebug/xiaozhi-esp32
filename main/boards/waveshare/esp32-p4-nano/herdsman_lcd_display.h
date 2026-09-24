#pragma once
/**
 * @file herdsman_lcd_display.h
 * @brief 牧场管理竖屏 UI:三栏布局(左Logo/中聊天/右按钮)+顶栏+设置页.
 *
 * 布局: 顶栏72px(网络/状态/静音/电池/时钟); 中部卡片为聊天列表,
 * 空闲时覆盖“推荐问题”待机面板; 右栏为“前往设置/开始对话”按钮.
 * 宽度按 1920 参考分辨率等比缩放, 见 `UpdateColumnWidths()`.
 * 配色全部取自 `config.h` 的 HERDSMAN_UI_COLOR_* 编译期色板.
 */

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
    static constexpr int kReferenceScreenWidth = 1920;
    static constexpr int kReferenceCenterWidth = 1000;
    static constexpr int kMaxChatBubbleWidth = 780;
    static constexpr int kMaxSystemBubbleWidth = 850;

    int side_column_width_ = 460;
    int content_width_ = kReferenceCenterWidth;
    int chat_bubble_width_ = kMaxChatBubbleWidth;
    int system_bubble_width_ = kMaxSystemBubbleWidth;

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

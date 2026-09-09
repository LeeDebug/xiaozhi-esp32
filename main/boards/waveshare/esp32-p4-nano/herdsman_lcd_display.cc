#include "herdsman_lcd_display.h"

#include "application.h"
#include "audio/audio_codec.h"
#include "board.h"
#include "boards/common/backlight.h"
#include "display/lvgl_display/lvgl_theme.h"

#include <material_symbols.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

extern "C" {
LV_IMAGE_DECLARE(herdsman_logo);
}

namespace {

constexpr uint32_t kOrange = 0xE84A0C;
constexpr uint32_t kDarkText = 0x15191F;
constexpr uint32_t kMutedText = 0x73777D;
constexpr uint32_t kPageBackground = 0xEEF0F2;
constexpr uint32_t kCardBackground = 0xFFFFFF;
constexpr uint32_t kUserBubble = 0xE1E3E5;
constexpr uint32_t kAssistantBubble = 0xFFFFFF;

void MakePlain(lv_obj_t* object) {
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollbar_mode(object, LV_SCROLLBAR_MODE_OFF);
}

void StyleCard(lv_obj_t* object, int radius) {
    lv_obj_set_style_bg_color(object, lv_color_hex(kCardBackground), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_90, 0);
    lv_obj_set_style_border_width(object, 1, 0);
    lv_obj_set_style_border_color(object, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_radius(object, radius, 0);
    lv_obj_set_style_shadow_width(object, 22, 0);
    lv_obj_set_style_shadow_opa(object, LV_OPA_20, 0);
    lv_obj_set_style_shadow_color(object, lv_color_hex(0x7F858B), 0);
    lv_obj_set_style_shadow_offset_y(object, 6, 0);
}

lv_obj_t* CreateIconTextButton(lv_obj_t* parent, const char* icon, const char* text,
                               const lv_font_t* icon_font, bool filled, lv_event_cb_t callback,
                               void* user_data) {
    lv_obj_t* button = lv_button_create(parent);
    lv_obj_set_size(button, 310, filled ? 90 : 82);
    lv_obj_set_style_radius(button, 20, 0);
    lv_obj_set_style_border_width(button, filled ? 0 : 3, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(kOrange), 0);
    lv_obj_set_style_bg_color(button,
                              filled ? lv_color_hex(kOrange) : lv_color_hex(kCardBackground), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(button, filled ? 18 : 0, 0);
    lv_obj_set_style_shadow_opa(button, filled ? LV_OPA_30 : LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_color(button, lv_color_hex(kOrange), 0);
    lv_obj_set_style_shadow_offset_y(button, 7, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0xC83D08), LV_STATE_PRESSED);
    lv_obj_set_flex_flow(button, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(button, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(button, 14, 0);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);

    lv_obj_t* icon_label = lv_label_create(button);
    lv_obj_set_style_text_font(icon_label, icon_font, 0);
    lv_obj_set_style_text_color(icon_label, filled ? lv_color_white() : lv_color_hex(kOrange), 0);
    lv_label_set_text(icon_label, icon);

    lv_obj_t* text_label = lv_label_create(button);
    lv_obj_set_style_text_color(text_label, filled ? lv_color_white() : lv_color_hex(kDarkText), 0);
    lv_label_set_text(text_label, text);
    return button;
}

lv_obj_t* CreateSettingsRow(lv_obj_t* parent, const char* icon, const char* title,
                            const lv_font_t* icon_font) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), 54);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_border_color(row, lv_color_hex(0xD9DDE0), LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* icon_label = lv_label_create(row);
    lv_obj_set_style_text_font(icon_label, icon_font, 0);
    lv_obj_set_style_text_color(icon_label, lv_color_hex(kOrange), 0);
    lv_label_set_text(icon_label, icon);
    lv_obj_align(icon_label, LV_ALIGN_LEFT_MID, 22, 0);

    lv_obj_t* title_label = lv_label_create(row);
    lv_obj_set_style_text_color(title_label, lv_color_hex(kDarkText), 0);
    lv_label_set_text(title_label, title);
    lv_obj_align(title_label, LV_ALIGN_LEFT_MID, 72, 0);
    return row;
}

lv_obj_t* CreateSettingsValue(lv_obj_t* row, const char* value) {
    lv_obj_t* label = lv_label_create(row);
    lv_obj_set_width(label, 360);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(kMutedText), 0);
    lv_label_set_text(label, value);
    lv_obj_align(label, LV_ALIGN_RIGHT_MID, -24, 0);
    return label;
}

void StyleSlider(lv_obj_t* slider) {
    lv_obj_set_size(slider, 300, 14);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0xD9DDE0), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(kOrange), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(kOrange), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 8, LV_PART_KNOB);
}

}  // namespace

HerdsmanLcdDisplay::HerdsmanLcdDisplay(esp_lcd_panel_io_handle_t panel_io,
                                       esp_lcd_panel_handle_t panel, int width, int height,
                                       int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                                       bool swap_xy)
    : MipiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x, mirror_y,
                     swap_xy) {}

void HerdsmanLcdDisplay::SetupUI() {
    if (setup_ui_called_) {
        return;
    }

    Display::SetupUI();
    DisplayLockGuard lock(this);

    auto* theme = static_cast<LvglTheme*>(current_theme_);
    const lv_font_t* text_font = theme->text_font()->font();
    lv_obj_t* screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, text_font, 0);
    lv_obj_set_style_text_color(screen, lv_color_hex(kDarkText), 0);
    lv_obj_set_style_bg_color(screen, lv_color_hex(kPageBackground), 0);

    container_ = lv_obj_create(screen);
    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(container_, lv_color_hex(kPageBackground), 0);
    lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(container_, 0, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_clear_flag(container_, LV_OBJ_FLAG_SCROLLABLE);

    CreateTopBar(container_);
    CreateHomeContent(container_);
    CreateSettingsPanel(screen);

    low_battery_popup_ = lv_obj_create(screen);
    lv_obj_set_size(low_battery_popup_, 720, 70);
    lv_obj_align(low_battery_popup_, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_obj_set_style_bg_color(low_battery_popup_, lv_color_hex(0xD93333), 0);
    lv_obj_set_style_border_width(low_battery_popup_, 0, 0);
    lv_obj_set_style_radius(low_battery_popup_, 18, 0);
    low_battery_label_ = lv_label_create(low_battery_popup_);
    lv_obj_set_style_text_color(low_battery_label_, lv_color_white(), 0);
    lv_label_set_text(low_battery_label_, "电量不足，请及时充电");
    lv_obj_center(low_battery_label_);
    lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);

    UpdateHomeState();
}

void HerdsmanLcdDisplay::CreateTopBar(lv_obj_t* parent) {
    auto* theme = static_cast<LvglTheme*>(current_theme_);
    const lv_font_t* icon_font = theme->large_icon_font()->font();

    top_bar_ = lv_obj_create(parent);
    lv_obj_set_pos(top_bar_, 12, 8);
    lv_obj_set_size(top_bar_, LV_HOR_RES - 24, kTopBarHeight);
    StyleCard(top_bar_, 18);
    lv_obj_set_style_pad_all(top_bar_, 0, 0);
    lv_obj_clear_flag(top_bar_, LV_OBJ_FLAG_SCROLLABLE);

    network_label_ = lv_label_create(top_bar_);
    lv_obj_set_style_text_font(network_label_, icon_font, 0);
    lv_obj_set_style_text_color(network_label_, lv_color_hex(0x3F4C5B), 0);
    lv_label_set_text(network_label_, MATERIAL_SYMBOLS_WIFI);
    lv_obj_align(network_label_, LV_ALIGN_LEFT_MID, 30, 0);

    status_bar_ = lv_obj_create(top_bar_);
    lv_obj_set_size(status_bar_, 1050, kTopBarHeight);
    MakePlain(status_bar_);
    lv_obj_center(status_bar_);

    status_label_ = lv_label_create(status_bar_);
    lv_obj_set_width(status_label_, 1000);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(status_label_, lv_color_hex(kOrange), 0);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(status_label_, "初始化中...");
    lv_obj_center(status_label_);

    notification_label_ = lv_label_create(status_bar_);
    lv_obj_set_width(notification_label_, 1000);
    lv_obj_set_style_text_align(notification_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(notification_label_, lv_color_hex(kOrange), 0);
    lv_label_set_text(notification_label_, "");
    lv_obj_center(notification_label_);
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    mute_label_ = lv_label_create(top_bar_);
    lv_obj_set_style_text_font(mute_label_, icon_font, 0);
    lv_obj_set_style_text_color(mute_label_, lv_color_hex(0x3F4C5B), 0);
    lv_label_set_text(mute_label_, "");
    lv_obj_align(mute_label_, LV_ALIGN_RIGHT_MID, -82, 0);

    battery_label_ = lv_label_create(top_bar_);
    lv_obj_set_style_text_font(battery_label_, icon_font, 0);
    lv_obj_set_style_text_color(battery_label_, lv_color_hex(0x3F4C5B), 0);
    lv_label_set_text(battery_label_, "");
    lv_obj_align(battery_label_, LV_ALIGN_RIGHT_MID, -28, 0);
}

void HerdsmanLcdDisplay::CreateHomeContent(lv_obj_t* parent) {
    auto* theme = static_cast<LvglTheme*>(current_theme_);
    const lv_font_t* icon_font = theme->large_icon_font()->font();

    lv_obj_t* left_panel = lv_obj_create(parent);
    lv_obj_set_pos(left_panel, 0, kTopBarHeight + 16);
    lv_obj_set_size(left_panel, kSideWidth, LV_VER_RES - kTopBarHeight - 16);
    MakePlain(left_panel);

    lv_obj_t* logo = lv_image_create(left_panel);
    lv_image_set_src(logo, &herdsman_logo);
    lv_obj_center(logo);

    content_ = lv_obj_create(parent);
    lv_obj_set_pos(content_, kSideWidth, kTopBarHeight + 16);
    lv_obj_set_size(content_, kCenterWidth, LV_VER_RES - kTopBarHeight - 16);
    MakePlain(content_);
    lv_obj_clear_flag(content_, LV_OBJ_FLAG_SCROLLABLE);

    chat_list_ = lv_obj_create(content_);
    lv_obj_set_size(chat_list_, kCenterWidth, LV_PCT(100));
    MakePlain(chat_list_);
    lv_obj_set_scroll_dir(chat_list_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(chat_list_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(chat_list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(chat_list_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(chat_list_, 14, 0);
    lv_obj_set_style_pad_row(chat_list_, 12, 0);

    CreateStandbyPanel();

    lv_obj_t* right_panel = lv_obj_create(parent);
    lv_obj_set_pos(right_panel, kSideWidth + kCenterWidth, kTopBarHeight + 16);
    lv_obj_set_size(right_panel, kSideWidth, LV_VER_RES - kTopBarHeight - 16);
    MakePlain(right_panel);
    lv_obj_clear_flag(right_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* settings_button =
        CreateIconTextButton(right_panel, MATERIAL_SYMBOLS_SETTINGS, "前往设置", icon_font, false,
                             SettingsButtonEvent, this);
    lv_obj_align(settings_button, LV_ALIGN_CENTER, 0, -62);

    lv_obj_t* chat_button = CreateIconTextButton(right_panel, MATERIAL_SYMBOLS_MIC, "开始对话",
                                                 icon_font, true, ChatButtonEvent, this);
    lv_obj_align(chat_button, LV_ALIGN_CENTER, 0, 66);
    chat_button_icon_ = lv_obj_get_child(chat_button, 0);
    chat_button_label_ = lv_obj_get_child(chat_button, 1);
}

void HerdsmanLcdDisplay::CreateStandbyPanel() {
    standby_panel_ = lv_obj_create(content_);
    lv_obj_set_size(standby_panel_, 720, 330);
    lv_obj_center(standby_panel_);
    StyleCard(standby_panel_, 22);
    lv_obj_set_style_pad_all(standby_panel_, 20, 0);
    lv_obj_clear_flag(standby_panel_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(standby_panel_);
    lv_obj_set_style_text_color(title, lv_color_hex(kDarkText), 0);
    lv_label_set_text(title, "推荐问题");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 6, 0);

    lv_obj_t* question_list = lv_obj_create(standby_panel_);
    lv_obj_set_pos(question_list, 0, 48);
    lv_obj_set_size(question_list, LV_PCT(100), 242);
    MakePlain(question_list);
    lv_obj_set_scroll_dir(question_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(question_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(question_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(question_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(question_list, 14, 0);

    static const char* questions[] = {
        "如何提高畜群的整体繁殖率？",       "这些设备如何进行日常维护？",
        "如果设备发生网络中断，该怎么办？", "这里是否有常见故障排查指南？",
        "怎样查看最近的设备运行数据？",     "如何设置更合适的环境告警阈值？",
    };
    for (const char* question : questions) {
        lv_obj_t* row = lv_obj_create(question_list);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        MakePlain(row);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 16, 0);

        lv_obj_t* bullet = lv_label_create(row);
        lv_obj_set_style_text_color(bullet, lv_color_hex(kOrange), 0);
        lv_label_set_text(bullet, "•");
        lv_obj_t* text = lv_label_create(row);
        lv_obj_set_width(text, 620);
        lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
        lv_label_set_text(text, question);
    }
}

void HerdsmanLcdDisplay::CreateSettingsPanel(lv_obj_t* parent) {
    auto* theme = static_cast<LvglTheme*>(current_theme_);
    const lv_font_t* icon_font = theme->large_icon_font()->font();

    settings_panel_ = lv_obj_create(parent);
    lv_obj_set_size(settings_panel_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(settings_panel_, lv_color_hex(kPageBackground), 0);
    lv_obj_set_style_bg_opa(settings_panel_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(settings_panel_, 0, 0);
    lv_obj_set_style_radius(settings_panel_, 0, 0);
    lv_obj_set_style_pad_all(settings_panel_, 0, 0);
    lv_obj_clear_flag(settings_panel_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* header = lv_obj_create(settings_panel_);
    lv_obj_set_pos(header, 12, 8);
    lv_obj_set_size(header, LV_HOR_RES - 24, kTopBarHeight);
    StyleCard(header, 18);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* back_button = lv_button_create(header);
    lv_obj_set_size(back_button, 72, 64);
    lv_obj_align(back_button, LV_ALIGN_LEFT_MID, 8, 0);
    lv_obj_set_style_bg_opa(back_button, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(back_button, 0, 0);
    lv_obj_add_event_cb(back_button, BackButtonEvent, LV_EVENT_CLICKED, this);
    lv_obj_t* back_icon = lv_label_create(back_button);
    lv_obj_set_style_text_font(back_icon, icon_font, 0);
    lv_obj_set_style_text_color(back_icon, lv_color_hex(kDarkText), 0);
    lv_label_set_text(back_icon, MATERIAL_SYMBOLS_ARROW_BACK);
    lv_obj_center(back_icon);

    lv_obj_t* header_title = lv_label_create(header);
    lv_label_set_text(header_title, "设置");
    lv_obj_set_style_text_color(header_title, lv_color_hex(kDarkText), 0);
    lv_obj_center(header_title);

    lv_obj_t* card = lv_obj_create(settings_panel_);
    lv_obj_set_pos(card, 160, 96);
    lv_obj_set_size(card, LV_HOR_RES - 320, 342);
    StyleCard(card, 22);
    lv_obj_set_style_pad_all(card, 6, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* row = CreateSettingsRow(card, MATERIAL_SYMBOLS_SCHEDULE, "系统时间", icon_font);
    settings_time_label_ = CreateSettingsValue(row, "--");

    row = CreateSettingsRow(card, MATERIAL_SYMBOLS_WIFI, "信号", icon_font);
    CreateSettingsValue(row, "强（满格）");

    row = CreateSettingsRow(card, MATERIAL_SYMBOLS_BRIGHTNESS_6, "亮度", icon_font);
    lv_obj_t* brightness_slider = lv_slider_create(row);
    StyleSlider(brightness_slider);
    lv_slider_set_range(brightness_slider, 10, 100);
    auto* backlight = Board::GetInstance().GetBacklight();
    int brightness = backlight == nullptr ? 75 : backlight->brightness();
    lv_slider_set_value(brightness_slider, std::max(10, brightness), LV_ANIM_OFF);
    lv_obj_align(brightness_slider, LV_ALIGN_RIGHT_MID, -132, 0);
    lv_obj_add_event_cb(brightness_slider, BrightnessSliderEvent, LV_EVENT_ALL, this);
    brightness_value_label_ = CreateSettingsValue(row, "");
    lv_obj_set_width(brightness_value_label_, 90);
    char value[12];
    std::snprintf(value, sizeof(value), "%d%%", brightness);
    lv_label_set_text(brightness_value_label_, value);

    row = CreateSettingsRow(card, MATERIAL_SYMBOLS_VOLUME_UP, "音量", icon_font);
    lv_obj_t* volume_slider = lv_slider_create(row);
    StyleSlider(volume_slider);
    lv_slider_set_range(volume_slider, 0, 100);
    int volume = Board::GetInstance().GetAudioCodec()->output_volume();
    lv_slider_set_value(volume_slider, volume, LV_ANIM_OFF);
    lv_obj_align(volume_slider, LV_ALIGN_RIGHT_MID, -132, 0);
    lv_obj_add_event_cb(volume_slider, VolumeSliderEvent, LV_EVENT_ALL, this);
    volume_value_label_ = CreateSettingsValue(row, "");
    lv_obj_set_width(volume_value_label_, 90);
    std::snprintf(value, sizeof(value), "%d%%", volume);
    lv_label_set_text(volume_value_label_, value);

    row = CreateSettingsRow(card, MATERIAL_SYMBOLS_MIC, "唤醒词", icon_font);
    CreateSettingsValue(row, "你好小智");

    row = CreateSettingsRow(card, MATERIAL_SYMBOLS_LINK, "485串口", icon_font);
    lv_obj_t* serial_value = CreateSettingsValue(row, "已连接 ✓");
    lv_obj_set_style_text_color(serial_value, lv_color_hex(0x279B55), 0);

    lv_obj_add_flag(settings_panel_, LV_OBJ_FLAG_HIDDEN);
}

void HerdsmanLcdDisplay::SetTheme(Theme* theme) {
    if (theme == nullptr) {
        return;
    }

    DisplayLockGuard lock(this);
    auto* lvgl_theme = static_cast<LvglTheme*>(theme);
    lv_obj_t* screen = lv_screen_active();
    if (screen != nullptr && lvgl_theme->text_font() != nullptr &&
        lvgl_theme->text_font()->font() != nullptr) {
        // Text font is inherited by the board-specific UI. Icon labels have an explicit icon font
        // and therefore remain unchanged when an asset bundle replaces the text font.
        lv_obj_set_style_text_font(screen, lvgl_theme->text_font()->font(), 0);
    }

    // The Herdsman interface deliberately keeps its fixed light brand palette for both saved
    // themes; only the font asset is refreshed here.
    Display::SetTheme(lvgl_theme);
}

void HerdsmanLcdDisplay::SetStatus(const char* status) {
    const auto state = Application::GetInstance().GetDeviceState();
    const char* visible_status = status;
    if (state == kDeviceStateIdle) {
        visible_status = "等待中...";
    } else if (state == kDeviceStateListening) {
        visible_status = "聆听中...";
    } else if (state == kDeviceStateSpeaking) {
        visible_status = "说话中...";
    }
    LvglDisplay::SetStatus(visible_status);

    DisplayLockGuard lock(this);
    UpdateHomeState();
}

void HerdsmanLcdDisplay::SetChatMessage(const char* role, const char* content) {
    if (!setup_ui_called_ || chat_list_ == nullptr || content == nullptr) {
        return;
    }

    DisplayLockGuard lock(this);
    if (content[0] == '\0') {
        return;
    }
    if (standby_panel_ != nullptr) {
        lv_obj_add_flag(standby_panel_, LV_OBJ_FLAG_HIDDEN);
    }

    constexpr uint32_t kMaxMessages = 30;
    if (lv_obj_get_child_cnt(chat_list_) >= kMaxMessages) {
        lv_obj_del(lv_obj_get_child(chat_list_, 0));
    }

    const bool is_user = std::strcmp(role, "user") == 0;
    const bool is_assistant = std::strcmp(role, "assistant") == 0;
    lv_obj_t* row = lv_obj_create(chat_list_);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    MakePlain(row);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, is_user ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(row, 12, 0);

    auto create_avatar = [&](const char* icon, uint32_t background, uint32_t foreground) {
        lv_obj_t* avatar = lv_obj_create(row);
        lv_obj_set_size(avatar, 54, 54);
        lv_obj_set_style_radius(avatar, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(avatar, lv_color_hex(background), 0);
        lv_obj_set_style_bg_opa(avatar, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(avatar, 0, 0);
        lv_obj_set_style_pad_all(avatar, 0, 0);
        lv_obj_clear_flag(avatar, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t* avatar_icon = lv_label_create(avatar);
        auto* theme = static_cast<LvglTheme*>(current_theme_);
        lv_obj_set_style_text_font(avatar_icon, theme->large_icon_font()->font(), 0);
        lv_obj_set_style_text_color(avatar_icon, lv_color_hex(foreground), 0);
        lv_label_set_text(avatar_icon, icon);
        lv_obj_center(avatar_icon);
    };

    if (is_assistant) {
        create_avatar(MATERIAL_SYMBOLS_ROBOT_2, 0xFFFFFF, kOrange);
    }

    lv_obj_t* bubble = lv_obj_create(row);
    lv_obj_set_width(bubble, (is_user || is_assistant) ? 620 : 680);
    lv_obj_set_height(bubble, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(bubble, 18, 0);
    lv_obj_set_style_border_width(bubble, 0, 0);
    lv_obj_set_style_pad_all(bubble, 14, 0);
    lv_obj_set_style_bg_color(
        bubble, lv_color_hex(is_user ? kUserBubble : (is_assistant ? kAssistantBubble : 0xFFF1E8)),
        0);
    lv_obj_set_style_bg_opa(bubble, LV_OPA_COVER, 0);
    lv_obj_clear_flag(bubble, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* text = lv_label_create(bubble);
    lv_obj_set_width(text, (is_user || is_assistant) ? 592 : 652);
    lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(text, lv_color_hex(kDarkText), 0);
    lv_label_set_text(text, content);
    chat_message_label_ = text;

    if (is_user) {
        create_avatar(MATERIAL_SYMBOLS_PERSON, 0xC7C9CB, 0x787B7F);
    }

    lv_obj_scroll_to_view_recursive(row, LV_ANIM_ON);
}

void HerdsmanLcdDisplay::ClearChatMessages() {
    DisplayLockGuard lock(this);
    if (chat_list_ != nullptr) {
        lv_obj_clean(chat_list_);
    }
    chat_message_label_ = nullptr;
    UpdateHomeState();
}

void HerdsmanLcdDisplay::SetEmotion(const char* emotion) { (void)emotion; }

void HerdsmanLcdDisplay::UpdateHomeState() {
    if (!setup_ui_called_ || standby_panel_ == nullptr) {
        return;
    }
    const auto state = Application::GetInstance().GetDeviceState();
    if (state == kDeviceStateIdle) {
        lv_obj_remove_flag(standby_panel_, LV_OBJ_FLAG_HIDDEN);
        if (chat_button_label_ != nullptr) {
            lv_label_set_text(chat_button_label_, "开始对话");
        }
        if (chat_button_icon_ != nullptr) {
            lv_label_set_text(chat_button_icon_, MATERIAL_SYMBOLS_MIC);
        }
    } else {
        lv_obj_add_flag(standby_panel_, LV_OBJ_FLAG_HIDDEN);
        if (chat_button_label_ != nullptr) {
            lv_label_set_text(chat_button_label_, "结束对话");
        }
        if (chat_button_icon_ != nullptr) {
            lv_label_set_text(chat_button_icon_, MATERIAL_SYMBOLS_STOP);
        }
    }
}

void HerdsmanLcdDisplay::UpdateSettingsTime() {
    if (settings_time_label_ == nullptr) {
        return;
    }
    const std::time_t now = std::time(nullptr);
    const std::tm* local_time = std::localtime(&now);
    if (local_time == nullptr || local_time->tm_year < 125) {
        lv_label_set_text(settings_time_label_, "时间未同步");
        return;
    }
    char time_text[32];
    std::strftime(time_text, sizeof(time_text), "%Y-%m-%d %H:%M", local_time);
    lv_label_set_text(settings_time_label_, time_text);
}

void HerdsmanLcdDisplay::ShowSettings(bool show) {
    if (settings_panel_ == nullptr) {
        return;
    }
    if (show) {
        UpdateSettingsTime();
        lv_obj_remove_flag(settings_panel_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(settings_panel_);
    } else {
        lv_obj_add_flag(settings_panel_, LV_OBJ_FLAG_HIDDEN);
    }
}

void HerdsmanLcdDisplay::SettingsButtonEvent(lv_event_t* event) {
    auto* self = static_cast<HerdsmanLcdDisplay*>(lv_event_get_user_data(event));
    self->ShowSettings(true);
}

void HerdsmanLcdDisplay::BackButtonEvent(lv_event_t* event) {
    auto* self = static_cast<HerdsmanLcdDisplay*>(lv_event_get_user_data(event));
    self->ShowSettings(false);
}

void HerdsmanLcdDisplay::ChatButtonEvent(lv_event_t* event) {
    (void)event;
    Application::GetInstance().ToggleChatState();
}

void HerdsmanLcdDisplay::BrightnessSliderEvent(lv_event_t* event) {
    auto* self = static_cast<HerdsmanLcdDisplay*>(lv_event_get_user_data(event));
    lv_obj_t* slider = lv_event_get_target_obj(event);
    const int value = lv_slider_get_value(slider);
    if (lv_event_get_code(event) == LV_EVENT_VALUE_CHANGED) {
        char text[12];
        std::snprintf(text, sizeof(text), "%d%%", value);
        lv_label_set_text(self->brightness_value_label_, text);
    } else if (lv_event_get_code(event) == LV_EVENT_RELEASED) {
        Application::GetInstance().Schedule([value]() {
            auto* backlight = Board::GetInstance().GetBacklight();
            if (backlight != nullptr) {
                backlight->SetBrightness(value, true);
            }
        });
    }
}

void HerdsmanLcdDisplay::VolumeSliderEvent(lv_event_t* event) {
    auto* self = static_cast<HerdsmanLcdDisplay*>(lv_event_get_user_data(event));
    lv_obj_t* slider = lv_event_get_target_obj(event);
    const int value = lv_slider_get_value(slider);
    if (lv_event_get_code(event) == LV_EVENT_VALUE_CHANGED) {
        char text[12];
        std::snprintf(text, sizeof(text), "%d%%", value);
        lv_label_set_text(self->volume_value_label_, text);
    } else if (lv_event_get_code(event) == LV_EVENT_RELEASED) {
        Application::GetInstance().Schedule(
            [value]() { Board::GetInstance().GetAudioCodec()->SetOutputVolume(value); });
    }
}

/**
 * @file herdsman_lcd_display.cc
 * @brief HerdsmanLcdDisplay 实现: LVGL 顶栏/三栏主页/待机问题/设置页/聊天气泡.
 * 配色 kAccent 等取自 config.h 色板; 左右栏受 HERDSMAN_UI_SHOW_* 裁剪内容区.
 */
#include "herdsman_lcd_display.h"
#include "herdsman_audio_meter.h"

#include "application.h"
#include "assets.h"
#include "audio/audio_codec.h"
#include "board.h"
#include "boards/common/backlight.h"
#include "config.h"
#include "display/lvgl_display/lvgl_theme.h"
#include "system_info.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <fb_gfx.h>
#include <material_symbols.h>
#include <src/misc/cache/lv_cache.h>
#include <wifi_manager.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

extern "C" {
LV_IMAGE_DECLARE(logo);
}

LV_FONT_DECLARE(font_herdsman_ui_30_4);

namespace {
// fb_gfx emits RGB565 high byte first; the LVGL RGB565 canvas needs native little endian.
uint16_t CanvasGfxColor(uint32_t rgb) {
    const uint16_t rgb565 = ((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F);
    return static_cast<uint16_t>((rgb565 << 8) | (rgb565 >> 8));
}

// 以 % 开头的 MCP / 函数调用中间态(如 "% self.production.lights.get...")不适合直接上屏.
bool IsToolCallNoise(const char* s) {
    if (s == nullptr || s[0] == '\0') {
        return true;
    }
    while (*s == ' ' || *s == '\n' || *s == '\r' || *s == '\t') {
        ++s;
    }
    if (s[0] == '%' || s[0] == '<' || s[0] == '{' || s[0] == '[') {
        return true;
    }
    auto contains = [&](const char* needle) { return std::strstr(s, needle) != nullptr; };
    if (contains("self.") || contains("self,") || contains("production.") ||
        contains("lights.get") || contains("lights.set") || contains("mcp__") ||
        contains("tool_call") || contains("function_call") || contains("()") || contains("```")) {
        return true;
    }
    return false;
}

// 工具调用 -> 中文提示映射表: 新增功能只加一行, 按顺序优先匹配.
// keyword 为服务端下发文本中的子串(精确工具名放前面, 泛关键词放后面).
struct ToolCallFriendlyEntry {
    const char* keyword;
    const char* text;
};
constexpr ToolCallFriendlyEntry kToolCallFriendlyTable[] = {
    {"silos.get_info", "正在查询料塔重量…"},
    {"main_water_valve.get", "正在查询水阀状态…"},
    {"main_water_valve.set", "正在控制水阀…"},
    {"water_temperature.get", "正在查询水温…"},
    {"water_temperature", "正在查询水温…"},
    {"lights.get", "正在查询灯光状态…"},
    {"lights.set", "正在控制所有灯光…"},
    {"light.get", "正在查询灯光状态…"},
    {"light_1", "正在控制灯光1…"},
    {"light_2", "正在控制灯光2…"},
    {"silo", "正在查询料塔重量…"},
    {"temperature", "正在查询水温…"},
    {"water_valve", "正在控制水阀…"},
    {"valve", "正在控制水阀…"},
    {"light", "正在查询灯光状态…"},
    {"switch", "正在控制开关…"},
};
constexpr char kToolCallFriendlyFallback[] = "正在查询设备状态…";

// 工具调用噪声统一转成中文提示, 避免屏幕出现英文代码.
const char* FriendlyToolCallText(const char* s) {
    for (const auto& entry : kToolCallFriendlyTable) {
        if (std::strstr(s, entry.keyword) != nullptr) {
            return entry.text;
        }
    }
    return kToolCallFriendlyFallback;
}

/* ===== 主题色板本地别名:全部取自 config.h HERDSMAN_UI_COLOR_*, 切主题只需改 HERDSMAN_UI_THEME
 * ===== */
constexpr uint32_t kAccent = HERDSMAN_UI_COLOR_ACCENT;                 // 主强调:按钮/高亮/发送键
constexpr uint32_t kAccentPressed = HERDSMAN_UI_COLOR_ACCENT_PRESSED;  // 按压态强调
constexpr uint32_t kOnAccent = HERDSMAN_UI_COLOR_ON_ACCENT;            // 强调色上白字/白图标
constexpr uint32_t kText = HERDSMAN_UI_COLOR_TEXT;                     // 主文字
constexpr uint32_t kMutedText = HERDSMAN_UI_COLOR_MUTED_TEXT;          // 次要文字/占位/时间
constexpr uint32_t kIcon = HERDSMAN_UI_COLOR_ICON;                     // 图标色
constexpr uint32_t kPageBackground = HERDSMAN_UI_COLOR_PAGE;           // 页面底
constexpr uint32_t kCardBackground = HERDSMAN_UI_COLOR_CARD;           // 卡片底
constexpr uint32_t kCardBorder = HERDSMAN_UI_COLOR_CARD_BORDER;        // 卡片边框
constexpr uint32_t kDivider = HERDSMAN_UI_COLOR_DIVIDER;               // 分割线
constexpr uint32_t kShadow = HERDSMAN_UI_COLOR_SHADOW;                 // 阴影
constexpr uint32_t kAssistantAvatarBackground =
    HERDSMAN_UI_COLOR_ASSISTANT_AVATAR_BG;                                    // 助手头像底
constexpr uint32_t kUserAvatarBackground = HERDSMAN_UI_COLOR_USER_AVATAR_BG;  // 用户头像底
constexpr uint32_t kUserAvatarForeground = HERDSMAN_UI_COLOR_USER_AVATAR_FG;  // 用户头像字
constexpr uint32_t kSuccess = HERDSMAN_UI_COLOR_SUCCESS;                      // 成功绿(在线/联网)
constexpr uint32_t kDanger = HERDSMAN_UI_COLOR_DANGER;                        // 危险红(错误/离线)

/** @brief 去卡片化:清边框/内边距/背景透明并关滚动条, 用于顶栏图标等裸控件. */
void MakePlain(lv_obj_t* object) {
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollbar_mode(object, LV_SCROLLBAR_MODE_OFF);
}

/** @brief 卡片统一样式:主题底色+90%不透明+1px边框+22px柔阴影, radius 为圆角. */
void StyleCard(lv_obj_t* object, int radius) {
    lv_obj_set_style_bg_color(object, lv_color_hex(kCardBackground), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_90, 0);
    lv_obj_set_style_border_width(object, 1, 0);
    lv_obj_set_style_border_color(object, lv_color_hex(kCardBorder), 0);
    lv_obj_set_style_radius(object, radius, 0);
    lv_obj_set_style_shadow_width(object, 22, 0);
    lv_obj_set_style_shadow_opa(object, LV_OPA_20, 0);
    lv_obj_set_style_shadow_color(object, lv_color_hex(kShadow), 0);
    lv_obj_set_style_shadow_offset_y(object, 6, 0);
}

#if HERDSMAN_UI_SHOW_LEFT_LOGO
/** @brief 左栏图标+文字按钮:filled=实心强调底/白字, 否则白底描边; 点击回调经 user_data 回传. */
lv_obj_t* CreateIconTextButton(lv_obj_t* parent, const char* icon, const char* text,
                               const lv_font_t* text_font, const lv_font_t* icon_font, bool filled,
                               int width, lv_event_cb_t callback, void* user_data) {
    lv_obj_t* button = lv_button_create(parent);
    lv_obj_set_size(button, width, filled ? 90 : 82);
    lv_obj_set_style_radius(button, 20, 0);
    lv_obj_set_style_border_width(button, filled ? 0 : 3, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(kAccent), 0);
    lv_obj_set_style_bg_color(button,
                              filled ? lv_color_hex(kAccent) : lv_color_hex(kCardBackground), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(button, filled ? 18 : 0, 0);
    lv_obj_set_style_shadow_opa(button, filled ? LV_OPA_30 : LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_color(button, lv_color_hex(kAccent), 0);
    lv_obj_set_style_shadow_offset_y(button, 7, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(kAccentPressed), LV_STATE_PRESSED);
    lv_obj_set_flex_flow(button, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(button, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(button, 14, 0);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);

    lv_obj_t* icon_label = lv_label_create(button);
    lv_obj_set_style_text_font(icon_label, icon_font, 0);
    lv_obj_set_style_text_color(icon_label,
                                filled ? lv_color_hex(kOnAccent) : lv_color_hex(kAccent), 0);
    lv_label_set_text(icon_label, icon);

    lv_obj_t* text_label = lv_label_create(button);
    lv_obj_set_style_text_font(text_label, text_font, 0);
    lv_obj_set_style_text_color(text_label, filled ? lv_color_hex(kOnAccent) : lv_color_hex(kText),
                                0);
    lv_label_set_text(text_label, text);
    return button;
}
#endif

/** @brief 设置页单行:透明底+底部 1px 分割线+左图标槽, 不可滚动点击, 内容由调用方填充. */
lv_obj_t* CreateSettingsRow(lv_obj_t* parent, const char* icon, const char* title,
                            const lv_font_t* icon_font) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), 54);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_border_color(row, lv_color_hex(kDivider), LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* icon_label = lv_label_create(row);
    lv_obj_set_style_text_font(icon_label, icon_font, 0);
    lv_obj_set_style_text_color(icon_label, lv_color_hex(kAccent), 0);
    lv_label_set_text(icon_label, icon);
    lv_obj_align(icon_label, LV_ALIGN_LEFT_MID, 22, 0);

    lv_obj_t* title_label = lv_label_create(row);
    lv_obj_set_style_text_color(title_label, lv_color_hex(kText), 0);
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
    lv_obj_set_style_bg_color(slider, lv_color_hex(kDivider), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(kAccent), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(kAccent), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 8, LV_PART_KNOB);
}

}  // namespace

HerdsmanLcdDisplay::HerdsmanLcdDisplay(esp_lcd_panel_io_handle_t panel_io,
                                       esp_lcd_panel_handle_t panel, int width, int height,
                                       int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                                       bool swap_xy)
    : MipiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x, mirror_y,
                     swap_xy) {
    auto* theme = static_cast<LvglTheme*>(current_theme_);
    if (theme != nullptr && theme->text_font() != nullptr) {
        theme->text_font()->SetFallback(&font_herdsman_ui_30_4);
    }
}

HerdsmanLcdDisplay::~HerdsmanLcdDisplay() {
    DisplayLockGuard lock(this);
    if (audio_meter_timer_ != nullptr) {
        lv_timer_delete(audio_meter_timer_);
    }
    if (audio_canvas_ != nullptr) {
        lv_image_cache_drop(&audio_canvas_draw_buf_);
        lv_obj_delete(audio_canvas_);
    }
    heap_caps_free(audio_canvas_buffer_);
}

void HerdsmanLcdDisplay::SetupUI() {
    if (setup_ui_called_) {
        return;
    }

    // Apply the complete font from the assets partition before any labels capture a font pointer.
    auto& assets = Assets::GetInstance();
    if (assets.partition_valid()) {
        assets.Apply(false);
    }

    Display::SetupUI();
    DisplayLockGuard lock(this);

    auto* theme = static_cast<LvglTheme*>(current_theme_);
    const lv_font_t* text_font = theme->text_font()->font();
    lv_obj_t* screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, text_font, 0);
    lv_obj_set_style_text_color(screen, lv_color_hex(kText), 0);
    lv_obj_set_style_bg_color(screen, lv_color_hex(kPageBackground), 0);

    container_ = lv_obj_create(screen);
    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(container_, lv_color_hex(kPageBackground), 0);
    lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(container_, 0, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_clear_flag(container_, LV_OBJ_FLAG_SCROLLABLE);

    UpdateColumnWidths();
    CreateTopBar(container_);
    CreateHomeContent(container_);
    CreateSettingsPanel(screen);

    low_battery_popup_ = lv_obj_create(screen);
    lv_obj_set_size(low_battery_popup_, 720, 70);
    lv_obj_align(low_battery_popup_, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_obj_set_style_bg_color(low_battery_popup_, lv_color_hex(kDanger), 0);
    lv_obj_set_style_border_width(low_battery_popup_, 0, 0);
    lv_obj_set_style_radius(low_battery_popup_, 18, 0);
    low_battery_label_ = lv_label_create(low_battery_popup_);
    lv_obj_set_style_text_color(low_battery_label_, lv_color_hex(kOnAccent), 0);
    lv_label_set_text(low_battery_label_, "电量不足，请及时充电");
    lv_obj_center(low_battery_label_);
    lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);

    audio_meter_timer_ = lv_timer_create(
        [](lv_timer_t* timer) {
            static_cast<HerdsmanLcdDisplay*>(lv_timer_get_user_data(timer))->UpdateAudioMeter();
        },
        25, this);
    UpdateDateTime();
    UpdateSignalStrength();
    UpdateHomeState();
    lv_obj_update_layout(screen);
    lv_obj_invalidate(screen);
}

void HerdsmanLcdDisplay::CreateTopBar(lv_obj_t* parent) {
    auto* theme = static_cast<LvglTheme*>(current_theme_);
    const lv_font_t* icon_font = theme->large_icon_font()->font();

    top_bar_ = lv_obj_create(parent);
    lv_obj_set_pos(top_bar_, 12, 8);
    lv_obj_set_size(top_bar_, LV_HOR_RES - 24, kTopBarHeight);
    lv_obj_set_style_border_width(top_bar_, 0, 0);
    lv_obj_set_style_shadow_width(top_bar_, 0, 0);
    lv_obj_set_style_radius(top_bar_, 18, 0);
    lv_obj_set_style_bg_color(top_bar_, lv_color_hex(kPageBackground), 0);
    lv_obj_set_style_bg_opa(top_bar_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(top_bar_, 0, 0);
    lv_obj_clear_flag(top_bar_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* brand = lv_image_create(top_bar_);
    lv_image_set_src(brand, &logo);
    const int logo_width = std::min(side_column_width_ - 32, 368);
    lv_obj_set_size(brand, logo_width, 60);
    lv_image_set_inner_align(brand, LV_IMAGE_ALIGN_CONTAIN);
    lv_obj_align(brand, LV_ALIGN_LEFT_MID, 16, 0);

    network_label_ = lv_label_create(top_bar_);
    lv_obj_set_style_text_font(network_label_, icon_font, 0);
    lv_obj_set_style_text_color(network_label_, lv_color_hex(kIcon), 0);
    lv_label_set_text(network_label_, MATERIAL_SYMBOLS_WIFI);
    lv_obj_align(network_label_, LV_ALIGN_RIGHT_MID, -92, 0);

    status_bar_ = lv_obj_create(top_bar_);
    // Reserve the right-hand clock and icon slots so status text cannot overlap them.
    constexpr int kClockWidth = 360;
    constexpr int kClockRightOffset = 256;
    const int status_width = std::max(120, static_cast<int>(LV_HOR_RES) - 24 - side_column_width_ -
                                               kClockWidth - kClockRightOffset - 24);
    lv_obj_set_size(status_bar_, status_width, kTopBarHeight);
    MakePlain(status_bar_);
    lv_obj_align(status_bar_, LV_ALIGN_LEFT_MID, side_column_width_ + 8, 0);

    status_label_ = lv_label_create(status_bar_);
    lv_obj_set_width(status_label_, LV_PCT(100));
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(status_label_, lv_color_hex(kAccent), 0);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(status_label_, "初始化中...");
    lv_obj_center(status_label_);

    notification_label_ = lv_label_create(status_bar_);
    lv_obj_set_width(notification_label_, LV_PCT(100));
    lv_obj_set_style_text_align(notification_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(notification_label_, lv_color_hex(kAccent), 0);
    lv_label_set_text(notification_label_, "");
    lv_obj_center(notification_label_);
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    mute_label_ = lv_label_create(top_bar_);
    lv_obj_set_style_text_font(mute_label_, icon_font, 0);
    lv_obj_set_style_text_color(mute_label_, lv_color_hex(kIcon), 0);
    lv_label_set_text(mute_label_, "");
    lv_obj_align(mute_label_, LV_ALIGN_RIGHT_MID, -200, 0);

    battery_label_ = lv_label_create(top_bar_);
    lv_obj_set_style_text_font(battery_label_, icon_font, 0);
    lv_obj_set_style_text_color(battery_label_, lv_color_hex(kIcon), 0);
    lv_label_set_text(battery_label_, "");
    lv_obj_align(battery_label_, LV_ALIGN_RIGHT_MID, -148, 0);

    date_time_label_ = lv_label_create(top_bar_);
    lv_obj_set_width(date_time_label_, kClockWidth);
    lv_obj_set_style_text_align(date_time_label_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(date_time_label_, lv_color_hex(kMutedText), 0);
    lv_label_set_text(date_time_label_, "----/--/-- --:--:--");
    lv_obj_align(date_time_label_, LV_ALIGN_RIGHT_MID, -kClockRightOffset, 0);

    lv_obj_t* settings_button = lv_button_create(top_bar_);
    lv_obj_set_size(settings_button, 64, 56);
    lv_obj_align(settings_button, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_obj_set_style_radius(settings_button, 16, 0);
    lv_obj_set_style_bg_color(settings_button, lv_color_hex(kPageBackground), 0);
    lv_obj_set_style_bg_color(settings_button, lv_color_hex(kAccent), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(settings_button, 2, 0);
    lv_obj_set_style_border_color(settings_button, lv_color_hex(kAccent), 0);
    lv_obj_set_style_shadow_width(settings_button, 0, 0);
    lv_obj_add_event_cb(settings_button, SettingsButtonEvent, LV_EVENT_CLICKED, this);

    settings_button_icon_ = lv_label_create(settings_button);
    lv_obj_set_style_text_font(settings_button_icon_, icon_font, 0);
    lv_obj_set_style_text_color(settings_button_icon_, lv_color_hex(kAccent), 0);
    lv_label_set_text(settings_button_icon_, MATERIAL_SYMBOLS_SETTINGS);
    lv_obj_center(settings_button_icon_);
}

void HerdsmanLcdDisplay::UpdateColumnWidths() {
    side_column_width_ = LV_HOR_RES * 20 / 100;
    right_column_width_ = LV_HOR_RES * 30 / 100;
    const int left_width = HERDSMAN_UI_SHOW_LEFT_LOGO ? side_column_width_ : 0;
    const int right_width = HERDSMAN_UI_SHOW_RIGHT_BUTTONS ? right_column_width_ : 0;
    content_width_ = LV_HOR_RES - left_width - right_width;

    constexpr int kChatListHorizontalPadding = 28;
    constexpr int kAvatarAndGapWidth = 66;
    chat_bubble_width_ =
        std::min(kMaxChatBubbleWidth,
                 std::max(120, content_width_ - kChatListHorizontalPadding - kAvatarAndGapWidth));
    system_bubble_width_ =
        std::min(kMaxSystemBubbleWidth, std::max(120, content_width_ - kChatListHorizontalPadding));
}

void HerdsmanLcdDisplay::CreateHomeContent(lv_obj_t* parent) {
#if HERDSMAN_UI_SHOW_LEFT_LOGO
    CreateAudioPanel(parent);
#endif
    const int left_panel_width = HERDSMAN_UI_SHOW_LEFT_LOGO ? side_column_width_ : 0;

    content_ = lv_obj_create(parent);
    lv_obj_set_pos(content_, left_panel_width, kTopBarHeight + 24);
    lv_obj_set_size(content_, content_width_, LV_VER_RES - kTopBarHeight - 40);
    lv_obj_set_style_border_width(content_, 0, 0);
    lv_obj_set_style_shadow_width(content_, 0, 0);
    lv_obj_set_style_radius(content_, 24, 0);
    lv_obj_set_style_bg_color(content_, lv_color_hex(kPageBackground), 0);
    lv_obj_set_style_bg_opa(content_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(content_, 0, 0);
    lv_obj_clear_flag(content_, LV_OBJ_FLAG_SCROLLABLE);

    chat_list_ = lv_obj_create(content_);
    lv_obj_set_size(chat_list_, LV_PCT(100), LV_PCT(100));
    MakePlain(chat_list_);
    lv_obj_set_scroll_dir(chat_list_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(chat_list_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(chat_list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(chat_list_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(chat_list_, 14, 0);
    lv_obj_set_style_pad_row(chat_list_, 12, 0);

    CreateStandbyPanel();

#if HERDSMAN_UI_SHOW_RIGHT_BUTTONS
    CreateProductionChart(parent);
#endif
}

void HerdsmanLcdDisplay::CreateAudioPanel(lv_obj_t* parent) {
#if HERDSMAN_UI_SHOW_LEFT_LOGO
    auto* theme = static_cast<LvglTheme*>(current_theme_);
    lv_obj_t* panel = lv_obj_create(parent);
    lv_obj_set_pos(panel, 0, kTopBarHeight + 24);
    lv_obj_set_size(panel, side_column_width_, LV_VER_RES - kTopBarHeight - 40);
    MakePlain(panel);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    audio_meter_height_ = std::min(180, (static_cast<int>(LV_VER_RES) - kTopBarHeight - 160) / 2);
    const int spacing = std::max(3, (side_column_width_ - 48) / kAudioBarCount);
    const int bar_width = std::max(2, spacing / 3);
    audio_canvas_width_ = (kAudioBarCount - 1) * spacing + bar_width + 4;
    audio_canvas_height_ = std::max(
        3,
        std::min(audio_meter_height_ * 2, static_cast<int>(LV_VER_RES) - kTopBarHeight - 40 - 112));
    const uint32_t stride =
        lv_draw_buf_width_to_stride(audio_canvas_width_, LV_COLOR_FORMAT_RGB565);
    const size_t buffer_size = stride * audio_canvas_height_;
    const size_t alignment = std::max<size_t>(LV_DRAW_BUF_ALIGN, alignof(uint16_t));
    audio_canvas_buffer_ = static_cast<uint8_t*>(
        heap_caps_aligned_alloc(alignment, buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (audio_canvas_buffer_ == nullptr) {
        audio_canvas_buffer_ =
            static_cast<uint8_t*>(heap_caps_aligned_alloc(alignment, buffer_size, MALLOC_CAP_8BIT));
    }
    if (audio_canvas_buffer_ != nullptr &&
        lv_draw_buf_init(&audio_canvas_draw_buf_, audio_canvas_width_, audio_canvas_height_,
                         LV_COLOR_FORMAT_RGB565, stride, audio_canvas_buffer_,
                         buffer_size) == LV_RESULT_OK) {
        audio_canvas_ = lv_canvas_create(panel);
        lv_obj_remove_style_all(audio_canvas_);
        lv_canvas_set_draw_buf(audio_canvas_, &audio_canvas_draw_buf_);
        lv_obj_clear_flag(audio_canvas_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(audio_canvas_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(audio_canvas_, LV_ALIGN_CENTER, 0, -48);
        DrawAudioMeter(100);
    } else {
        heap_caps_free(audio_canvas_buffer_);
        audio_canvas_buffer_ = nullptr;
        ESP_LOGE("HerdsmanDisplay", "Unable to allocate audio canvas (%u bytes)",
                 static_cast<unsigned>(buffer_size));
    }
    lv_obj_t* chat_button =
        CreateIconTextButton(panel, MATERIAL_SYMBOLS_MIC, "开始对话", theme->text_font()->font(),
                             theme->large_icon_font()->font(), true,
                             std::min(280, side_column_width_ - 32), ChatButtonEvent, this);
    lv_obj_set_height(chat_button, 64);
    lv_obj_align(chat_button, LV_ALIGN_BOTTOM_MID, 0, -12);
    chat_button_icon_ = lv_obj_get_child(chat_button, 0);
    chat_button_label_ = lv_obj_get_child(chat_button, 1);
#endif
}

void HerdsmanLcdDisplay::UpdateAudioMeter() {
    const int input = HerdsmanAudioMeter::Consume(false);
    // Drain playback telemetry without displaying it.
    HerdsmanAudioMeter::Consume(true);
    const auto state = Application::GetInstance().GetDeviceState();
    if (state != kDeviceStateListening) {
        audio_level_ = 0;
        DrawAudioMeter(100);
        return;
    }
    const int target = input;
    // Fast attack and gentle release, with no allocations or audio reads in the UI task.
    audio_level_ = target > audio_level_ ? target : (audio_level_ * 7 + target) / 8;
    if (audio_canvas_ == nullptr) {
        return;
    }
    // Double the previous 2x excursion, preserving the quiet silhouette.
    const int scale =
        std::min(100 + 4 * audio_level_, audio_canvas_height_ * 1100 / audio_meter_height_);
    DrawAudioMeter(scale);
}

void HerdsmanLcdDisplay::DrawAudioMeter(int scale) {
    if (audio_canvas_ == nullptr || scale == audio_canvas_last_scale_) {
        return;
    }
    constexpr int profile[kAudioBarCount] = {22, 12, 18, 34, 16, 10, 24,  30, 20, 44, 90,
                                             50, 34, 42, 58, 28, 62, 100, 70, 44, 24, 16,
                                             12, 38, 20, 10, 16, 28, 36,  20, 26, 18, 12};
    // fb_gfx has no stride field. Include row padding in its logical width,
    // while LVGL keeps the visible canvas width in the image header.
    fb_data_t framebuffer = {};
    framebuffer.width = static_cast<int>(audio_canvas_draw_buf_.header.stride / 2);
    framebuffer.height = audio_canvas_height_;
    framebuffer.bytes_per_pixel = 2;
    framebuffer.format = FB_RGB565;
    framebuffer.data = audio_canvas_buffer_;
    fb_gfx_fillRect(&framebuffer, 0, 0, framebuffer.width, framebuffer.height,
                    CanvasGfxColor(kPageBackground));
    const int spacing = std::max(3, (side_column_width_ - 48) / kAudioBarCount);
    const int bar_width = std::max(2, spacing / 3);
    for (int i = 0; i < kAudioBarCount; ++i) {
        const int height =
            std::clamp(profile[i] * audio_meter_height_ * scale / 110000, 3, audio_canvas_height_);
        const int x = 2 + i * spacing;
        const int y = (audio_canvas_height_ - height) / 2;
        fb_gfx_fillRect(&framebuffer, x, y, bar_width, height, CanvasGfxColor(kAccent));
    }
    audio_canvas_last_scale_ = scale;
    lv_draw_buf_flush_cache(&audio_canvas_draw_buf_, nullptr);
    lv_image_cache_drop(&audio_canvas_draw_buf_);
    // Only one fixed canvas area is invalidated; no per-bar layout updates.
    lv_obj_invalidate(audio_canvas_);
}

void HerdsmanLcdDisplay::CreateProductionChart(lv_obj_t* parent) {
    lv_obj_t* panel = lv_obj_create(parent);
    const int left_width = HERDSMAN_UI_SHOW_LEFT_LOGO ? side_column_width_ : 0;
    lv_obj_set_pos(panel, left_width + content_width_ + 8, kTopBarHeight + 24);
    const int width = right_column_width_ - 20;
    const int height = LV_VER_RES - kTopBarHeight - 40;
    lv_obj_set_size(panel, width, height);
    MakePlain(panel);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    constexpr uint32_t colors[] = {0x398BFF, 0xA56AFF, 0x50C878};
    constexpr int maxima[] = {1500, 200, 2500};
    constexpr const char* names[] = {"料", "水", "蛋"};
    constexpr int data[3][7] = {{900, 650, 420, 1050, 1100, 460, 1008},
                                {20, 150, 175, 120, 112, 25, 108},
                                {1800, 1400, 500, 1300, 2200, 600, 1888}};
    for (int i = 0; i < 3; ++i) {
        lv_obj_t* value = lv_label_create(panel);
        lv_obj_set_pos(value, i * width / 3, 0);
        lv_obj_set_width(value, width / 3);
        lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(value, lv_color_hex(colors[i]), 0);
        lv_label_set_text_fmt(value, "%s\n%d", names[i], data[i][6]);
    }

    const int axis_width = std::clamp(width / 8, 44, 64);
    const int plot_x = axis_width * 2;
    const int plot_y = 140;
    const int plot_width = width - axis_width * 3 - 4;
    const int plot_height = height - plot_y - 48;
    lv_obj_t* chart = lv_chart_create(panel);
    lv_obj_set_pos(chart, plot_x, plot_y);
    lv_obj_set_size(chart, plot_width, plot_height);
    lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(chart, 0, 0);
    lv_obj_set_style_pad_all(chart, 0, 0);
    lv_obj_set_style_line_color(chart, lv_color_hex(kDivider), LV_PART_MAIN);
    lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_size(chart, 6, 6, LV_PART_INDICATOR);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, 7);
    lv_chart_set_div_line_count(chart, 5, 7);
    lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 1000);
    // LVGL supplies two Y axes. Normalize each series to draw three true independent scales.
    for (int i = 0; i < 3; ++i) {
        auto* series = lv_chart_add_series(chart, lv_color_hex(colors[i]), LV_CHART_AXIS_PRIMARY_Y);
        for (int j = 0; j < 7; ++j) {
            lv_chart_set_series_value_by_id(chart, series, j, data[i][j] * 1000 / maxima[i]);
        }
        const int x = i < 2 ? i * axis_width : plot_x + plot_width + 4;
        for (int tick = 0; tick < 5; ++tick) {
            lv_obj_t* label = lv_label_create(panel);
            lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(label, lv_color_hex(colors[i]), 0);
            lv_obj_set_width(label, axis_width - 2);
            lv_obj_set_style_text_align(label, i < 2 ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_LEFT, 0);
            lv_label_set_text_fmt(label, "%d", maxima[i] * (4 - tick) / 4);
            lv_obj_set_pos(label, x, plot_y + tick * plot_height / 4 - 8);
        }
    }
    for (int day = 0; day < 7; ++day) {
        lv_obj_t* label = lv_label_create(panel);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(kMutedText), 0);
        lv_label_set_text_fmt(label, "%d", day + 1);
        lv_obj_set_pos(label, plot_x + day * plot_width / 6 - 4, plot_y + plot_height + 12);
    }
    lv_chart_refresh(chart);
}

void HerdsmanLcdDisplay::CreateStandbyPanel() {
    standby_panel_ = lv_obj_create(content_);
    lv_obj_set_size(standby_panel_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(standby_panel_, 0, 0);
    lv_obj_set_style_bg_color(standby_panel_, lv_color_hex(kPageBackground), 0);
    lv_obj_set_style_bg_opa(standby_panel_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(standby_panel_, 0, 0);
    lv_obj_set_style_radius(standby_panel_, 24, 0);
    lv_obj_set_style_shadow_width(standby_panel_, 0, 0);
    lv_obj_set_style_pad_all(standby_panel_, 20, 0);
    lv_obj_clear_flag(standby_panel_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(standby_panel_);
    lv_obj_set_style_text_color(title, lv_color_hex(kText), 0);
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
        lv_obj_set_style_text_color(bullet, lv_color_hex(kAccent), 0);
        lv_label_set_text(bullet, "•");
        lv_obj_t* text = lv_label_create(row);
        lv_obj_set_width(text, 1);
        lv_obj_set_flex_grow(text, 1);
        lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_color(text, lv_color_hex(kText), 0);
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
    lv_obj_set_style_text_color(back_icon, lv_color_hex(kText), 0);
    lv_label_set_text(back_icon, MATERIAL_SYMBOLS_ARROW_BACK);
    lv_obj_center(back_icon);

    lv_obj_t* header_title = lv_label_create(header);
    lv_label_set_text(header_title, "设置");
    lv_obj_set_style_text_color(header_title, lv_color_hex(kText), 0);
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
    signal_value_label_ = CreateSettingsValue(row, "-- dBm");

    row = CreateSettingsRow(card, MATERIAL_SYMBOLS_BRIGHTNESS_6, "亮度", icon_font);
    brightness_slider_ = lv_slider_create(row);
    StyleSlider(brightness_slider_);
    lv_slider_set_range(brightness_slider_, 10, 100);
    auto* backlight = Board::GetInstance().GetBacklight();
    int brightness = backlight == nullptr ? 75 : backlight->brightness();
    lv_slider_set_value(brightness_slider_, std::max(10, brightness), LV_ANIM_OFF);
    lv_obj_align(brightness_slider_, LV_ALIGN_RIGHT_MID, -132, 0);
    lv_obj_add_event_cb(brightness_slider_, BrightnessSliderEvent, LV_EVENT_ALL, this);
    brightness_value_label_ = CreateSettingsValue(row, "");
    lv_obj_set_width(brightness_value_label_, 90);
    char value[12];
    std::snprintf(value, sizeof(value), "%d%%", brightness);
    lv_label_set_text(brightness_value_label_, value);

    row = CreateSettingsRow(card, MATERIAL_SYMBOLS_VOLUME_UP, "音量", icon_font);
    volume_slider_ = lv_slider_create(row);
    StyleSlider(volume_slider_);
    lv_slider_set_range(volume_slider_, 0, 100);
    int volume = Board::GetInstance().GetAudioCodec()->output_volume();
    lv_slider_set_value(volume_slider_, volume, LV_ANIM_OFF);
    lv_obj_align(volume_slider_, LV_ALIGN_RIGHT_MID, -132, 0);
    lv_obj_add_event_cb(volume_slider_, VolumeSliderEvent, LV_EVENT_ALL, this);
    volume_value_label_ = CreateSettingsValue(row, "");
    lv_obj_set_width(volume_value_label_, 90);
    std::snprintf(value, sizeof(value), "%d%%", volume);
    lv_label_set_text(volume_value_label_, value);

    row = CreateSettingsRow(card, MATERIAL_SYMBOLS_MIC, "唤醒词", icon_font);
    CreateSettingsValue(row, "小牧博士");

    row = CreateSettingsRow(card, MATERIAL_SYMBOLS_LINK, "485串口", icon_font);
    lv_obj_t* serial_value = CreateSettingsValue(row, "已连接 ✓");
    lv_obj_set_style_text_color(serial_value, lv_color_hex(kSuccess), 0);

    lv_obj_add_flag(settings_panel_, LV_OBJ_FLAG_HIDDEN);
}

void HerdsmanLcdDisplay::SetTheme(Theme* theme) {
    if (theme == nullptr) {
        return;
    }

    DisplayLockGuard lock(this);
    auto* lvgl_theme = static_cast<LvglTheme*>(theme);
    lv_obj_t* screen = lv_screen_active();
    const lv_font_t* text_font = nullptr;
    if (screen != nullptr && lvgl_theme->text_font() != nullptr &&
        lvgl_theme->text_font()->font() != nullptr) {
        text_font = lvgl_theme->text_font()->font();
        lv_obj_set_style_text_font(screen, text_font, 0);
        if (settings_button_label_ != nullptr) {
            lv_obj_set_style_text_font(settings_button_label_, text_font, 0);
        }
        if (chat_button_label_ != nullptr) {
            lv_obj_set_style_text_font(chat_button_label_, text_font, 0);
        }
    }

    const lv_font_t* icon_font =
        lvgl_theme->large_icon_font() != nullptr ? lvgl_theme->large_icon_font()->font() : nullptr;
    if (icon_font != nullptr) {
        if (settings_button_icon_ != nullptr) {
            lv_obj_set_style_text_font(settings_button_icon_, icon_font, 0);
        }
        if (chat_button_icon_ != nullptr) {
            lv_obj_set_style_text_font(chat_button_icon_, icon_font, 0);
        }
    }
    if (screen != nullptr) {
        lv_obj_update_layout(screen);
        lv_obj_invalidate(screen);
    }

    // The Herdsman interface keeps the compile-time palette selected in config.h; runtime theme
    // changes only refresh the font assets.
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

void HerdsmanLcdDisplay::UpdateStatusBar(bool update_all) {
    LvglDisplay::UpdateStatusBar(update_all);

    DisplayLockGuard lock(this);
    UpdateDateTime();
    UpdateSignalStrength();
}

void HerdsmanLcdDisplay::SetChatMessage(const char* role, const char* content) {
    if (!setup_ui_called_ || chat_list_ == nullptr || content == nullptr) {
        return;
    }
    // Keep startup board/version metadata out of the conversation on this dashboard.
    if (std::strcmp(role, "system") == 0 && SystemInfo::GetUserAgent() == content) {
        return;
    }

    DisplayLockGuard lock(this);
    if (content[0] == '\0') {
        return;
    }
    const bool is_user = std::strcmp(role, "user") == 0;
    const bool is_assistant = std::strcmp(role, "assistant") == 0;
    const char* visible_text = content;
    std::string friendly;
    if (is_assistant && IsToolCallNoise(content)) {
        friendly = FriendlyToolCallText(content);
        visible_text = friendly.c_str();
        // 工具调用中文提示作为 assistant 新气泡追加, 保留历史, 查多次则新增多次.
    }
    if (standby_panel_ != nullptr) {
        lv_obj_add_flag(standby_panel_, LV_OBJ_FLAG_HIDDEN);
    }

    constexpr uint32_t kMaxMessages = 30;
    if (lv_obj_get_child_cnt(chat_list_) >= kMaxMessages) {
        lv_obj_del(lv_obj_get_child(chat_list_, 0));
    }

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
        create_avatar(MATERIAL_SYMBOLS_ROBOT_2, kAssistantAvatarBackground, kAccent);
    }

    lv_obj_t* bubble = lv_obj_create(row);
    const int bubble_width = (is_user || is_assistant) ? chat_bubble_width_ : system_bubble_width_;
    lv_obj_set_width(bubble, bubble_width);
    lv_obj_set_height(bubble, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(bubble, 18, 0);
    lv_obj_set_style_border_width(bubble, 0, 0);
    lv_obj_set_style_pad_all(bubble, 14, 0);
    lv_obj_set_style_bg_color(bubble, lv_color_hex(kPageBackground), 0);
    lv_obj_set_style_bg_opa(bubble, LV_OPA_COVER, 0);
    lv_obj_clear_flag(bubble, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* text = lv_label_create(bubble);
    lv_obj_set_width(text, bubble_width - 28);
    lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(text, lv_color_hex(kText), 0);
    lv_label_set_text(text, visible_text);
    chat_message_label_ = text;

    if (is_user) {
        create_avatar(MATERIAL_SYMBOLS_PERSON, kUserAvatarBackground, kUserAvatarForeground);
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
    } else {
        lv_obj_add_flag(standby_panel_, LV_OBJ_FLAG_HIDDEN);
    }

    // Reset immediately on state changes rather than waiting for the next meter tick.
    if (state != kDeviceStateListening) {
        audio_level_ = 0;
        HerdsmanAudioMeter::Consume(false);
        HerdsmanAudioMeter::Consume(true);
        DrawAudioMeter(100);
    }

    const bool chat_active = state == kDeviceStateConnecting || state == kDeviceStateListening ||
                             state == kDeviceStateSpeaking;
    if (!chat_active) {
        if (chat_button_label_ != nullptr) {
            lv_label_set_text(chat_button_label_, "开始对话");
        }
        if (chat_button_icon_ != nullptr) {
            lv_label_set_text(chat_button_icon_, MATERIAL_SYMBOLS_MIC);
        }
    } else {
        if (chat_button_label_ != nullptr) {
            lv_label_set_text(chat_button_label_, "结束对话");
        }
        if (chat_button_icon_ != nullptr) {
            lv_label_set_text(chat_button_icon_, MATERIAL_SYMBOLS_STOP);
        }
    }
}

void HerdsmanLcdDisplay::UpdateDateTime() {
    if (date_time_label_ == nullptr) {
        return;
    }

    const std::time_t now = std::time(nullptr);
    const std::tm* local_time = std::localtime(&now);
    if (local_time == nullptr || local_time->tm_year < 125) {
        lv_label_set_text(date_time_label_, "----/--/-- --:--:--");
        return;
    }

    char time_text[32];
    std::strftime(time_text, sizeof(time_text), "%Y-%m-%d %H:%M:%S", local_time);
    lv_label_set_text(date_time_label_, time_text);
}

void HerdsmanLcdDisplay::UpdateSignalStrength() {
    if (signal_value_label_ == nullptr) {
        return;
    }

    auto& wifi = WifiManager::GetInstance();
    if (!wifi.IsConnected()) {
        lv_label_set_text(signal_value_label_, "-- dBm");
        return;
    }

    const int rssi = wifi.GetRssi();
    const char* level = rssi >= -65 ? "强" : (rssi >= -75 ? "中" : "弱");
    char signal_text[32];
    std::snprintf(signal_text, sizeof(signal_text), "%s（%d dBm）", level, rssi);
    lv_label_set_text(signal_value_label_, signal_text);
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

void HerdsmanLcdDisplay::UpdateSettingsValues() {
    auto* backlight = Board::GetInstance().GetBacklight();
    if (backlight != nullptr && brightness_slider_ != nullptr &&
        brightness_value_label_ != nullptr) {
        const int brightness = std::max(10, static_cast<int>(backlight->brightness()));
        lv_slider_set_value(brightness_slider_, brightness, LV_ANIM_OFF);
        char brightness_text[12];
        std::snprintf(brightness_text, sizeof(brightness_text), "%d%%", brightness);
        lv_label_set_text(brightness_value_label_, brightness_text);
    }

    auto* codec = Board::GetInstance().GetAudioCodec();
    if (codec != nullptr && volume_slider_ != nullptr && volume_value_label_ != nullptr) {
        const int volume = codec->output_volume();
        lv_slider_set_value(volume_slider_, volume, LV_ANIM_OFF);
        char volume_text[12];
        std::snprintf(volume_text, sizeof(volume_text), "%d%%", volume);
        lv_label_set_text(volume_value_label_, volume_text);
    }
}

void HerdsmanLcdDisplay::ShowSettings(bool show) {
    if (settings_panel_ == nullptr) {
        return;
    }
    if (show) {
        UpdateSettingsTime();
        UpdateSignalStrength();
        UpdateSettingsValues();
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
                // permanent=true persists to the existing display/brightness NVS key.
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
        Application::GetInstance().Schedule([value]() {
            // AudioCodec persists to the existing audio/output_volume NVS key.
            Board::GetInstance().GetAudioCodec()->SetOutputVolume(value);
        });
    }
}

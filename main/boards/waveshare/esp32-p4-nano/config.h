/*
 * @file config.h
 * @brief ESP32-P4-Nano 板级配置总表:音频/按键/Modbus/LCD/显示/Herdsman UI 开关与色板.
 *
 * 使用说明:
 * - 音频: ES8311 经 I2C 控制 + I2S 传输, 采样率 24kHz, 引脚见 AUDIO_* 定义.
 * - 按键: BOOT_BUTTON_GPIO 为配网/对话复用键(见 esp32-p4-nano.cc InitializeButtons).
 * - 产线: PRODUCTION_MODBUS_* 为 UART1 的端口与 TX/RX 引脚, 波特率等见 production_modbus.cc.
 * - 屏幕: LCD_TYPE 二选一 —— OTA7290B(8.8寸 480x1920) / JD9365(10.1寸 800x1280),
 *   均为 16bpp RGB565 + 2-lane MIPI-DSI; DISPLAY_* 控制 LVGL 旋转/镜像/偏移.
 * - UI: HERDSMAN_UI_SHOW_* 裁剪左右栏给聊天区让位; HERDSMAN_UI_THEME 三选一色板(默认 DARK).
 */
#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h> // GPIO_NUM_x / GPIO_NUM_NC 类型定义
#include <driver/uart.h> // UART_NUM_1 等串口号定义

/* ===== 音频采样率:输入(麦克风)与输出(喇叭)均为 24kHz, 与 ES8311 编解码配置一致 ===== */
#define AUDIO_INPUT_SAMPLE_RATE  24000  // 麦克风采集采样率(Hz)
#define AUDIO_OUTPUT_SAMPLE_RATE 24000  // 喇叭播放采样率(Hz)

/* ===== 音频 I2S 引脚: MCLK 主时钟 / WS 声道选择 / BCLK 位时钟 / DIN(采)/DOUT(放) ===== */
#define AUDIO_I2S_GPIO_MCLK GPIO_NUM_13 // 主时钟输出给 ES8311
#define AUDIO_I2S_GPIO_WS GPIO_NUM_10   // 左右声道切换(LRCK)
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_12 // I2S 位时钟
#define AUDIO_I2S_GPIO_DIN  GPIO_NUM_11 // MCU <- Codec, 麦克风数据入
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_9  // MCU -> Codec, 播放数据出

/* ===== 音频 Codec 控制:功放使能 + I2C 总线 + ES8311 从机地址 ===== */
#define AUDIO_CODEC_PA_PIN       GPIO_NUM_53             // 功放 PA 使能脚, 拉高出声
#define AUDIO_CODEC_I2C_SDA_PIN  GPIO_NUM_7              // Codec/触摸/摄像头/背光共用 I2C1 的 SDA
#define AUDIO_CODEC_I2C_SCL_PIN  GPIO_NUM_8              // 共用 I2C1 的 SCL
#define AUDIO_CODEC_ES8311_ADDR  ES8311_CODEC_DEFAULT_ADDR // ES8311 默认 7 位 I2C 地址

/* ===== 按键: BOOT 键 —— 启动期短按进配网, 运行期短按切换对话(见 InitializeButtons) ===== */
#define BOOT_BUTTON_GPIO        GPIO_NUM_35

/* ===== 产线 Modbus: UART1 半双工 RS485(仅 TX/RX, 靠外部自动收发切换, 无 DE/RE 脚) ===== */
#define PRODUCTION_MODBUS_UART_PORT   UART_NUM_1 // 固定用 UART1
#define PRODUCTION_MODBUS_UART_TX_PIN GPIO_NUM_20 // RS485 DI
#define PRODUCTION_MODBUS_UART_RX_PIN GPIO_NUM_21 // RS485 RO

/* ===== LCD 面板型号枚举: 0=8.8寸 OTA7290B, 1=10.1寸 JD9365 ===== */
#define LCD_TYPE_OTA7290B (0)
#define LCD_TYPE_JD9365 (1)

// 8.8 inch LCD --> LCD_TYPE_OTA7290B
// 10.1 inch LCD --> LCD_TYPE_JD9365
#define LCD_TYPE LCD_TYPE_OTA7290B // 当前装配的面板, 切换后 DISPLAY_WIDTH/HEIGHT 自动跟随

/* ===== 显示分辨率:随 LCD_TYPE 自动展开(O TA7290B 竖条屏 480x1920 / JD9365 800x1280) ===== */
#if LCD_TYPE == LCD_TYPE_OTA7290B
#define DISPLAY_WIDTH 480   // OTA7290B 横向像素
#define DISPLAY_HEIGHT 1920 // OTA7290B 纵向像素(竖屏长条)
#elif LCD_TYPE == LCD_TYPE_JD9365
#define DISPLAY_WIDTH 800   // JD9365 横向像素
#define DISPLAY_HEIGHT 1280 // JD9365 纵向像素
#else
#error "Unsupported LCD_TYPE" // 防止误填, 编译期直接报错
#endif

#define LCD_BIT_PER_PIXEL          (16)        // RGB565, 与 DPI in/out color format 对应
#define PIN_NUM_LCD_RST            GPIO_NUM_NC // 面板无独立复位脚, 靠上电时序复位

#define DELAY_TIME_MS                      (3000) // 通用延时基准(ms), 如亮屏/切屏等待
#define LCD_MIPI_DSI_LANE_NUM          (2)    // MIPI-DSI 2 条数据 lane, 与 bus_config.num_data_lanes 一致

/* ===== MIPI-DSI PHY 供电: 内部 LDO 通道与电压, 上电前必须先 bsp_enable_dsi_phy_power() ===== */
#define MIPI_DSI_PHY_PWR_LDO_CHAN          (3)
#define MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV    (2500) // 2.5V PHY 电压

/* ===== LVGL 显示方向:三者全 false 即竖屏原生方向; 改其一会旋转/镜像整屏 UI ===== */
#define DISPLAY_SWAP_XY false // true=横竖交换
#define DISPLAY_MIRROR_X false // true=左右镜像
#define DISPLAY_MIRROR_Y false // true=上下镜像

/* ===== 显示偏移:面板有效区起点, 0/0 即贴左上无裁剪 ===== */
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0

/* ===== 背光:由 CustomBacklight 经 I2C(0x45/0x96) 控制, 故此处无 PWM 脚, 保持 NC ===== */
#define DISPLAY_BACKLIGHT_PIN GPIO_NUM_NC
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false // 无 GPIO 背光, 反相标志保留仅为接口兼容

/* Herdsman UI 布局开关: 置 0 即隐藏该栏并把空间让给中间聊天区(见 herdsman_lcd_display.cc).
 * SHOW_LEFT_LOGO=左栏青岛天尚logo; SHOW_RIGHT_BUTTONS=右栏“前往设置/开始对话”按钮. */
#define HERDSMAN_UI_SHOW_LEFT_LOGO     1
#define HERDSMAN_UI_SHOW_RIGHT_BUTTONS 1
#if (HERDSMAN_UI_SHOW_LEFT_LOGO != 0 && HERDSMAN_UI_SHOW_LEFT_LOGO != 1) || \
    (HERDSMAN_UI_SHOW_RIGHT_BUTTONS != 0 && HERDSMAN_UI_SHOW_RIGHT_BUTTONS != 1)
#error "Herdsman UI visibility switches must be 0 or 1" // 仅允许 0/1, 防误填
#endif

/* Herdsman UI color theme selection.
 * 主题总览(色系说明):
 * - RANCH_ORANGE (0) : 牧场橙 / 暖色浅色主题. 以牧场橙 0xE84A0C 为品牌强调色,
 *                      页面底 0xEEF0F2 浅灰, 卡片纯白, 暖色系统气泡 0xFFF1E8.
 *                      适合白天/高亮展示用的默认浅色主题.
 * - TECH_BLUE (1)    : 科技蓝 / 冷色浅色主题. 以科技蓝 0x1769E0 为强调色,
 *                      页面底 0xEDF3FA 淡蓝灰, 用户气泡/系统气泡均为淡蓝色系
 *                      (0xD9E8FF / 0xE8F1FF). 适合科技感、冷静风格的浅色 UI.
 * - DARK (2, 默认)   : 深色主题 / 暗夜暖橙主题. 页面底 0x11151B 近黑,
 *                      卡片 0x20262E 深灰, 强调色为亮暖橙 0xFF7A45
 *                      (暗背景上保持对比度), 文字为浅色 0xF2F4F7.
 *                      适合夜间/低光环境的默认主题(当前 HERDSMAN_UI_THEME 选用此项).
 * 切换方法: 修改 HERDSMAN_UI_THEME 为上述三者之一即可, 下方 #if 会自动展开对应色板.
 */
#define HERDSMAN_UI_THEME_RANCH_ORANGE 0 // 牧场橙-浅色暖系
#define HERDSMAN_UI_THEME_TECH_BLUE    1 // 科技蓝-浅色冷系
#define HERDSMAN_UI_THEME_DARK         2 // 暗夜橙-深色系(默认)
#define HERDSMAN_UI_THEME              HERDSMAN_UI_THEME_DARK // 当前生效主题

#if HERDSMAN_UI_THEME == HERDSMAN_UI_THEME_RANCH_ORANGE
/* RANCH_ORANGE 牧场橙浅色主题色板: 浅灰页面 + 白卡 + 牧场橙强调 */
#define HERDSMAN_UI_COLOR_ACCENT              0xE84A0C // 强调色: 牧场橙(主按钮/高亮/发送键)
#define HERDSMAN_UI_COLOR_ACCENT_PRESSED      0xC83D08 // 强调色按下态: 深牧场橙(按钮按压反馈)
#define HERDSMAN_UI_COLOR_ON_ACCENT           0xFFFFFF // 强调色上文字/图标: 纯白(保证对比度)
#define HERDSMAN_UI_COLOR_TEXT                0x15191F // 主文字: 近黑
#define HERDSMAN_UI_COLOR_MUTED_TEXT          0x73777D // 次要文字: 中灰(时间戳/提示/占位)
#define HERDSMAN_UI_COLOR_ICON                0x3F4C5B // 图标: 深灰蓝
#define HERDSMAN_UI_COLOR_PAGE                0xEEF0F2 // 页面背景: 浅灰
#define HERDSMAN_UI_COLOR_CARD                0xFFFFFF // 卡片背景: 白(聊天列表/设置卡片)
#define HERDSMAN_UI_COLOR_CARD_BORDER         0xFFFFFF // 卡片边框: 白(无边框感, 靠阴影区分)
#define HERDSMAN_UI_COLOR_DIVIDER             0xD9DDE0 // 分割线: 浅灰
#define HERDSMAN_UI_COLOR_SHADOW              0x7F858B // 阴影: 中灰
#define HERDSMAN_UI_COLOR_USER_BUBBLE         0xE1E3E5 // 用户气泡: 浅灰
#define HERDSMAN_UI_COLOR_ASSISTANT_BUBBLE    0xFFFFFF // 助手气泡: 白
#define HERDSMAN_UI_COLOR_SYSTEM_BUBBLE       0xFFF1E8 // 系统气泡: 浅暖橙(提示/告警类消息)
#define HERDSMAN_UI_COLOR_ASSISTANT_AVATAR_BG 0xFFFFFF // 助手头像底: 白
#define HERDSMAN_UI_COLOR_USER_AVATAR_BG      0xC7C9CB // 用户头像底: 浅灰
#define HERDSMAN_UI_COLOR_USER_AVATAR_FG      0x787B7F // 用户头像字: 深灰
#define HERDSMAN_UI_COLOR_SUCCESS             0x279B55 // 成功: 牧场绿(在线/联网成功)
#define HERDSMAN_UI_COLOR_DANGER              0xD93333 // 危险: 警示红(错误/离线/删除)
#elif HERDSMAN_UI_THEME == HERDSMAN_UI_THEME_TECH_BLUE
/* TECH_BLUE 科技蓝浅色主题色板: 淡蓝灰页面 + 白卡 + 科技蓝强调 */
#define HERDSMAN_UI_COLOR_ACCENT              0x1769E0 // 强调色: 科技蓝(主按钮/高亮)
#define HERDSMAN_UI_COLOR_ACCENT_PRESSED      0x0E4FAF // 强调色按下态: 深蓝
#define HERDSMAN_UI_COLOR_ON_ACCENT           0xFFFFFF // 强调色上文字/图标: 纯白
#define HERDSMAN_UI_COLOR_TEXT                0x152033 // 主文字: 深藏青(带蓝调的黑)
#define HERDSMAN_UI_COLOR_MUTED_TEXT          0x667085 // 次要文字: 蓝灰
#define HERDSMAN_UI_COLOR_ICON                0x344054 // 图标: 深蓝灰
#define HERDSMAN_UI_COLOR_PAGE                0xEDF3FA // 页面背景: 淡蓝灰
#define HERDSMAN_UI_COLOR_CARD                0xFFFFFF // 卡片背景: 白
#define HERDSMAN_UI_COLOR_CARD_BORDER         0xDCE7F5 // 卡片边框: 淡蓝(浅色主题下描边可见)
#define HERDSMAN_UI_COLOR_DIVIDER             0xD6E0EC // 分割线: 淡蓝灰
#define HERDSMAN_UI_COLOR_SHADOW              0x6680A0 // 阴影: 蓝灰
#define HERDSMAN_UI_COLOR_USER_BUBBLE         0xD9E8FF // 用户气泡: 浅蓝
#define HERDSMAN_UI_COLOR_ASSISTANT_BUBBLE    0xFFFFFF // 助手气泡: 白
#define HERDSMAN_UI_COLOR_SYSTEM_BUBBLE       0xE8F1FF // 系统气泡: 极淡蓝
#define HERDSMAN_UI_COLOR_ASSISTANT_AVATAR_BG 0xE8F1FF // 助手头像底: 极淡蓝
#define HERDSMAN_UI_COLOR_USER_AVATAR_BG      0xC9D8EA // 用户头像底: 雾蓝灰
#define HERDSMAN_UI_COLOR_USER_AVATAR_FG      0x52657A // 用户头像字: 深蓝灰
#define HERDSMAN_UI_COLOR_SUCCESS             0x18864B // 成功: 深科技绿
#define HERDSMAN_UI_COLOR_DANGER              0xCF3B3B // 危险: 警示红
#elif HERDSMAN_UI_THEME == HERDSMAN_UI_THEME_DARK
/* DARK 暗夜暖橙深色主题色板: 近黑页面 + 深灰卡 + 亮暖橙强调(默认生效) */
#define HERDSMAN_UI_COLOR_ACCENT              0xFF7A45 // 强调色: 亮暖橙(暗底高对比, 主按钮/高亮)
#define HERDSMAN_UI_COLOR_ACCENT_PRESSED      0xD95B2B // 强调色按下态: 深暖橙
#define HERDSMAN_UI_COLOR_ON_ACCENT           0xFFFFFF // 强调色上文字/图标: 纯白
#define HERDSMAN_UI_COLOR_TEXT                0xF2F4F7 // 主文字: 浅灰白(暗底可读)
#define HERDSMAN_UI_COLOR_MUTED_TEXT          0xA7ADB7 // 次要文字: 中浅灰
#define HERDSMAN_UI_COLOR_ICON                0xD0D5DD // 图标: 浅灰
#define HERDSMAN_UI_COLOR_PAGE                0x11151B // 页面背景: 近黑
#define HERDSMAN_UI_COLOR_CARD                0x20262E // 卡片背景: 深灰
#define HERDSMAN_UI_COLOR_CARD_BORDER         0x343C47 // 卡片边框: 炭灰(暗色下描边)
#define HERDSMAN_UI_COLOR_DIVIDER             0x3B4450 // 分割线: 深灰蓝
#define HERDSMAN_UI_COLOR_SHADOW              0x000000 // 阴影: 纯黑(深色主题投影)
#define HERDSMAN_UI_COLOR_USER_BUBBLE         0x39424D // 用户气泡: 深灰蓝
#define HERDSMAN_UI_COLOR_ASSISTANT_BUBBLE    0x282F38 // 助手气泡: 更深灰(与用户气泡区分层级)
#define HERDSMAN_UI_COLOR_SYSTEM_BUBBLE       0x3A2A24 // 系统气泡: 深暖棕(暗底上的暖色提示)
#define HERDSMAN_UI_COLOR_ASSISTANT_AVATAR_BG 0x343C47 // 助手头像底: 炭灰
#define HERDSMAN_UI_COLOR_USER_AVATAR_BG      0x4A5563 // 用户头像底: 中深灰
#define HERDSMAN_UI_COLOR_USER_AVATAR_FG      0xD0D5DD // 用户头像字: 浅灰
#define HERDSMAN_UI_COLOR_SUCCESS             0x4CCB7F // 成功: 亮绿(暗底可读性优化)
#define HERDSMAN_UI_COLOR_DANGER              0xE05252 // 危险: 亮红(暗底可读性优化)
#else
#error "Unsupported HERDSMAN_UI_THEME" // 主题编号越界时编译期报错
#endif

#endif // _BOARD_CONFIG_H_

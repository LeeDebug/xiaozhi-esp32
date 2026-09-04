#include "sdkconfig.h"

#if CONFIG_XIAOZHI_NETWORK_ETHERNET
#include "ethernet_board.h"
#else
#include "wifi_board.h"
#endif
#include "codecs/es8311_audio_codec.h"
#include "application.h"
#include "display/lcd_display.h"
// #include "display/no_display.h"
#include "button.h"

#include "esp_video.h"
#include "esp_video_init.h"
#include "esp_cam_sensor_xclk.h"

#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_ldo_regulator.h"

#include "esp_lcd_ota7290b.h"
#include "config.h"
#include "lcd_init_cmds.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/i2c_master.h>
#include <esp_lvgl_port.h>
#include "esp_lcd_touch_gt911.h"

extern "C" {
#include "bsp/esp32_p4_platform.h"
}

#define TAG "WaveshareEsp32p4nano"

#if CONFIG_XIAOZHI_NETWORK_ETHERNET
using WaveshareEsp32p4nanoBase = EthernetBoard;
#else
using WaveshareEsp32p4nanoBase = WifiBoard;
#endif

class I2cBacklight : public Backlight {
public:
    explicit I2cBacklight(i2c_master_bus_handle_t i2c_bus)
        : i2c_bus_(i2c_bus) {}

protected:
    void SetBrightnessImpl(uint8_t brightness) override {
        i2c_master_dev_handle_t device = nullptr;
        const i2c_device_config_t device_config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = LCD_BACKLIGHT_I2C_ADDRESS,
            .scl_speed_hz = 100000,
        };
        ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus_, &device_config, &device));
        const uint8_t payload[] = {LCD_BACKLIGHT_I2C_REGISTER, brightness};
        ESP_ERROR_CHECK(i2c_master_transmit(device, payload, sizeof(payload), 100));
        ESP_ERROR_CHECK(i2c_master_bus_rm_device(device));
    }

private:
    i2c_master_bus_handle_t i2c_bus_;
};

class WaveshareEsp32p4nano : public WaveshareEsp32p4nanoBase {
private:
    i2c_master_bus_handle_t codec_i2c_bus_;
    Button boot_button_;
    LcdDisplay *display__;
    EspVideo* camera_ = nullptr;
    I2cBacklight *backlight_;

    void InitializeCodecI2c() {
        // Initialize I2C peripheral
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_1,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &codec_i2c_bus_));
    }

    static esp_err_t bsp_enable_dsi_phy_power(void) {
#if MIPI_DSI_PHY_PWR_LDO_CHAN > 0
        // Turn on the power for MIPI DSI PHY, so it can go from "No Power" state to "Shutdown" state
        static esp_ldo_channel_handle_t phy_pwr_chan = NULL;
        esp_ldo_channel_config_t ldo_cfg = {
            .chan_id = MIPI_DSI_PHY_PWR_LDO_CHAN,
            .voltage_mv = MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV,
        };
        esp_ldo_acquire_channel(&ldo_cfg, &phy_pwr_chan);
        ESP_LOGI(TAG, "MIPI DSI PHY Powered on");
#endif // BSP_MIPI_DSI_PHY_PWR_LDO_CHAN > 0

        return ESP_OK;
    }

    void InitializeLCD() {
        bsp_enable_dsi_phy_power();
        esp_lcd_panel_io_handle_t io = NULL;
        esp_lcd_panel_handle_t disp_panel = NULL;

        esp_lcd_dsi_bus_handle_t mipi_dsi_bus = NULL;
        esp_lcd_dsi_bus_config_t bus_config = {
            .bus_id = 0,
            .num_data_lanes = 2,
            // The panel component's reference configuration uses 1300 Mbps.
            // 1500 Mbps can make DBI reads hang when the panel link is marginal.
            .lane_bit_rate_mbps = 1300,
        };
        ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus));

        ESP_LOGI(TAG, "Install MIPI DSI LCD control panel");
        // The OTA7290B component expects 8-bit DCS command and parameter fields.
        esp_lcd_dbi_io_config_t dbi_config = OTA7290B_PANEL_IO_DBI_CONFIG();
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &io));

        // Build the DPI configuration explicitly because the component macro uses
        // a nested designated initializer that GCC rejects in C++ mode.
        esp_lcd_dpi_panel_config_t dpi_config = {
            .virtual_channel = 0,
            .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
            .dpi_clock_freq_mhz = 75,
            .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
            .in_color_format = LCD_COLOR_FMT_RGB565,
            .out_color_format = LCD_COLOR_FMT_RGB565,
            .num_fbs = 1,
            .video_timing = {
                .h_size = 480,
                .v_size = 1920,
                .hsync_pulse_width = 50,
                .hsync_back_porch = 50,
                .hsync_front_porch = 50,
                .vsync_pulse_width = 20,
                .vsync_back_porch = 20,
                .vsync_front_porch = 20,
            },
            .flags = {
                .use_dma2d = true,
            },
        };

        ota7290b_vendor_config_t vendor_config = {
            .init_cmds = lcd_init_cmds,
            .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
            .mipi_config = {
                .dsi_bus = mipi_dsi_bus,
                .dpi_config = &dpi_config,
            },
        };

        const esp_lcd_panel_dev_config_t lcd_dev_config = {
            .reset_gpio_num = PIN_NUM_LCD_RST,
            .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
            .bits_per_pixel = 16,
            .vendor_config = &vendor_config,
        };
        ESP_ERROR_CHECK(esp_lcd_new_panel_ota7290b(io, &lcd_dev_config, &disp_panel));
        ESP_ERROR_CHECK(esp_lcd_panel_reset(disp_panel));
        ESP_ERROR_CHECK(esp_lcd_panel_init(disp_panel));
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(disp_panel, true));

        display__ = new MipiLcdDisplay(io, disp_panel, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                       DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
        backlight_ = new I2cBacklight(codec_i2c_bus_);
        backlight_->RestoreBrightness();
    }
    void InitializeTouch()
    {
        // This panel uses GT9271, which is compatible with the GT911 driver.
        // Probe both legal GT9xx addresses instead of creating a device blindly.
        esp_lcd_touch_handle_t tp = nullptr;
        esp_lcd_touch_config_t tp_cfg = {
            .x_max = DISPLAY_WIDTH,
            .y_max = DISPLAY_HEIGHT,
            .rst_gpio_num = GPIO_NUM_NC,
            .int_gpio_num = GPIO_NUM_NC,
            .levels = {
                .reset = 0,
                .interrupt = 0,
            },
            .flags = {
                .swap_xy = DISPLAY_SWAP_XY,
                .mirror_x = DISPLAY_MIRROR_X,
                .mirror_y = DISPLAY_MIRROR_Y,
            },
        };
        esp_lcd_panel_io_handle_t tp_io_handle = nullptr;
        esp_lcd_panel_io_i2c_config_t tp_io_config = {
            .dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS,
            .control_phase_bytes = 1,
            .dc_bit_offset = 0,
            .lcd_cmd_bits = 16,
            .lcd_param_bits = 8,
            .flags = {
                .disable_control_phase = 1,
            },
            .scl_speed_hz = 100 * 1000,
        };

        const uint8_t touch_addresses[] = {
            ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS,
            ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP,
        };
        esp_err_t touch_err = ESP_ERR_NOT_FOUND;
        for (uint8_t address : touch_addresses) {
            if (i2c_master_probe(codec_i2c_bus_, address, 100) != ESP_OK) {
                continue;
            }
            tp_io_config.dev_addr = address;
            touch_err = esp_lcd_new_panel_io_i2c(codec_i2c_bus_, &tp_io_config, &tp_io_handle);
            if (touch_err == ESP_OK) {
                ESP_LOGI(TAG, "GT9xx touch controller found at 0x%02X", address);
                break;
            }
        }
        ESP_ERROR_CHECK(touch_err);
        ESP_LOGI(TAG, "Initialize touch controller");
        ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, &tp));
        const lvgl_port_touch_cfg_t touch_cfg = {
            .disp = lv_display_get_default(),
            .handle = tp,
        };
        lvgl_port_add_touch(&touch_cfg);
        ESP_LOGI(TAG, "Touch panel initialized successfully");
    }
    void InitializeCamera() {
        esp_video_init_csi_config_t base_csi_config = {
            .sccb_config = {
                .init_sccb = false,
                .i2c_handle = codec_i2c_bus_,
                .freq = 400000,
            },
            .reset_pin = GPIO_NUM_NC,
            .pwdn_pin  = GPIO_NUM_NC,
        };

        esp_video_init_config_t cam_config = {
            .csi      = &base_csi_config,
        };

        camera_ = new EspVideo(cam_config);
    }
    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
#if CONFIG_XIAOZHI_NETWORK_ETHERNET
            if (app.GetDeviceState() != kDeviceStateStarting) {
                app.ToggleChatState();
            }
#else
            // During startup (before connected), pressing BOOT button enters Wi-Fi config mode without reboot
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
#endif
        });
    }

public:
    WaveshareEsp32p4nano() :
        boot_button_(BOOT_BUTTON_GPIO) {
        InitializeCodecI2c();
        InitializeLCD();
        InitializeTouch();
        InitializeCamera();
        InitializeButtons();
    }

    virtual AudioCodec *GetAudioCodec() override {
        static Es8311AudioCodec audio_codec(codec_i2c_bus_, I2C_NUM_1, AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
                                            AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN,
                                            AUDIO_CODEC_PA_PIN, AUDIO_CODEC_ES8311_ADDR);
        return &audio_codec;
    }

    virtual Display *GetDisplay() override {
        return display__;
    }

    virtual Camera* GetCamera() override {
        return camera_;
    }

    virtual Backlight *GetBacklight() override {
         return backlight_;
     }

};

DECLARE_BOARD(WaveshareEsp32p4nano);

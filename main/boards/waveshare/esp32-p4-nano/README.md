# Waveshare ESP32-P4-NANO


[ESP32-P4-NANO](https://www.waveshare.com/esp32-p4-nano.htm) is a small size and highly integrated development board designed by waveshare electronics based on ESP32-P4 chip

## Production-monitoring Modbus

This board starts an on-demand RTU master on UART1 (TX GPIO20, RX GPIO21) at 9600 baud, 8 data bits, no parity, and 1 stop bit. Automatic polling is currently disabled by `kAutomaticPollingEnabled`; set it to `true` to restore the polling task.

- MCP tools `self.production.main_water_valve.set` and `self.production.main_water_valve.get` write/read register 0x6B only when invoked. A successful function-code 0x06 response means the write command was accepted; current hardware state is queried separately with the getter.
- MCP tool `self.production.water_temperature.get` reads register 0x6C only when invoked and returns its value as Celsius using a 0.001 scale.
- MCP tools `self.production.light.get` and `self.production.lights.get` read one or both configured light channels from registers 0x3B-0x3C.
- MCP tools `self.production.light_1.open`, `self.production.light_1.close`, `self.production.light_2.open`, and `self.production.light_2.close` control the two light channels. Brightness is limited to 0-100 percent, where 0 means off.
- MCP tool `self.production.silos.get_info` reads the current signed 32-bit weights of silos 1-4 from register pairs 0x02/0x03, 0x0E/0x0F, 0x1A/0x1B, and 0x26/0x27. The low word is read first, as required by the protocol.
- Valve writes use protocol value 1 for open and 2 for closed.

The device address defaults to 0, which means not configured; Modbus reads and writes are disabled until `ProductionModbus::SetDeviceAddress(1..247)` is called. Successful on-demand reads are also retained in the cache and are available through `GetCachedRegister()` and `GetSnapshot()`. Reads and writes are serialized by a shared bus mutex.

Only TX and RX are configured. The external RS485 transceiver must provide automatic direction control. If the hardware instead requires a DE/RE signal, add its GPIO and switch the UART to RS485 half-duplex mode.



## Display Page


### Recommended display screen

| Product ID                                                                                                                                                                                                                                                                                               | Dependency                                                       | tested |
|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|------------------------------------------------------------------|--------|
| [10.1-DSI-TOUCH-A](https://www.waveshare.com/10.1-dsi-touch-a.htm) <br/><img style="width: 150px; height: auto; display: block; margin: 0 auto;" src="https://www.waveshare.com/media/catalog/product/cache/1/image/800x800/9df78eab33525d08d6e5fb8d27136e95/1/0/10.1-dsi-touch-a-1.jpg"> | [waveshare/esp_lcd_jd9365_10_1](https://github.com/waveshareteam/waveshare-ESP32-components/tree/master/display/lcd/esp_lcd_jd9365_10_1) | ✅      |
| [101M-8001280-IPS-CT-K](https://www.waveshare.com/101m-8001280-ips-ct-k.htm) <br/><img style="width: 150px; height: auto; display: block; margin: 0 auto;" src="https://www.waveshare.com/media/catalog/product/cache/1/image/800x800/9df78eab33525d08d6e5fb8d27136e95/1/0/101m-8001280-ips-ct-k-1.jpg"> | [waveshare/esp_lcd_jd9365_10_1](https://github.com/waveshareteam/waveshare-ESP32-components/tree/master/display/lcd/esp_lcd_jd9365_10_1) | ✅      |

### Common Raspberry adapter screen

**These displays are supported on [ESP32-P4-NANO BSP](https://github.com/waveshareteam/waveshare-ESP32-components/tree/master/bsp/esp32_p4_nano), but not on xiaozhi-esp32**

<details open>
<summary>View full display</summary>

| Product ID                                                                                                                                                                                                                                                                                               | Dependency                                                       | tested |
|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|------------------------------------------------------------------|--------|
| [2.8inch DSI LCD](https://www.waveshare.com/2.8inch-dsi-lcd.htm) <br/><img style="width: 150px; height: auto; display: block; margin: 0 auto;" src="https://www.waveshare.com/media/catalog/product/cache/1/thumbnail/122x122/9df78eab33525d08d6e5fb8d27136e95/2/_/2.8inch-dsi-lcd-3.jpg">               | [waveshare/esp_lcd_dsi](https://github.com/waveshareteam/waveshare-ESP32-components/tree/master/display/lcd/esp_lcd_dsi)                 | 🕒      |
| [3.4inch DSI LCD (C)](https://www.waveshare.com/3.4inch-dsi-lcd-c.htm) <br/><img style="width: 150px; height: auto; display: block; margin: 0 auto;" src="https://www.waveshare.com/media/catalog/product/cache/1/image/800x800/9df78eab33525d08d6e5fb8d27136e95/3/_/3.4inch-dsi-lcd-c-1.jpg">           | [waveshare/esp_lcd_dsi](https://github.com/waveshareteam/waveshare-ESP32-components/tree/master/display/lcd/esp_lcd_dsi)                 | 🕒      |
| [4inch DSI LCD (C)](https://www.waveshare.com/4inch-dsi-lcd-c.htm) <br/><img style="width: 150px; height: auto; display: block; margin: 0 auto;" src="https://www.waveshare.com/media/catalog/product/cache/1/image/800x800/9df78eab33525d08d6e5fb8d27136e95/4/i/4inch-dsi-lcd-c-1.jpg">                 | [waveshare/esp_lcd_dsi](https://github.com/waveshareteam/waveshare-ESP32-components/tree/master/display/lcd/esp_lcd_dsi)                 | 🕒      |
| [4inch DSI LCD](https://www.waveshare.com/4inch-dsi-lcd.htm) <br/><img style="width: 150px; height: auto; display: block; margin: 0 auto;" src="https://www.waveshare.com/media/catalog/product/cache/1/image/800x800/9df78eab33525d08d6e5fb8d27136e95/4/i/4inch-dsi-lcd-1.jpg">                         | [waveshare/esp_lcd_dsi](https://github.com/waveshareteam/waveshare-ESP32-components/tree/master/display/lcd/esp_lcd_dsi)                 | 🕒      |
| [5inch DSI LCD (D)](https://www.waveshare.com/5inch-dsi-lcd-d.htm) <br/><img style="width: 150px; height: auto; display: block; margin: 0 auto;" src="https://www.waveshare.com/media/catalog/product/cache/1/image/800x800/9df78eab33525d08d6e5fb8d27136e95/5/i/5inch-dsi-lcd-d-2.jpg">                 | [waveshare/esp_lcd_dsi](https://github.com/waveshareteam/waveshare-ESP32-components/tree/master/display/lcd/esp_lcd_dsi)                 | 🕒      |
| [6.25inch DSI LCD](https://www.waveshare.com/6.25inch-dsi-lcd.htm) <br/><img style="width: 150px; height: auto; display: block; margin: 0 auto;" src="https://www.waveshare.com/media/catalog/product/cache/1/image/800x800/9df78eab33525d08d6e5fb8d27136e95/6/_/6.25inch-dsi-lcd-2.jpg">                | [waveshare/esp_lcd_dsi](https://github.com/waveshareteam/waveshare-ESP32-components/tree/master/display/lcd/esp_lcd_dsi)                 | 🕒      |
| [5inch DSI LCD (C)](https://www.waveshare.com/5inch-dsi-lcd-c.htm) <br/><img style="width: 150px; height: auto; display: block; margin: 0 auto;" src="https://www.waveshare.com/media/catalog/product/cache/1/image/800x800/9df78eab33525d08d6e5fb8d27136e95/5/i/5inch-dsi-lcd-c-2.jpg">                 | [waveshare/esp_lcd_dsi](https://github.com/waveshareteam/waveshare-ESP32-components/tree/master/display/lcd/esp_lcd_dsi)                 | 🕒      |
| [7inch DSI LCD (C)](https://www.waveshare.com/7inch-dsi-lcd-c-with-case-a.htm) <br/><img style="width: 150px; height: auto; display: block; margin: 0 auto;" src="https://www.waveshare.com/media/catalog/product/cache/1/image/800x800/9df78eab33525d08d6e5fb8d27136e95/7/i/7inch-dsi-lcd-c-4.jpg">     | [waveshare/esp_lcd_dsi](https://github.com/waveshareteam/waveshare-ESP32-components/tree/master/display/lcd/esp_lcd_dsi)                 | 🕒      |
| [7.9inch DSI LCD](https://www.waveshare.com/7.9inch-dsi-lcd.htm) <br/><img style="width: 150px; height: auto; display: block; margin: 0 auto;" src="https://www.waveshare.com/media/catalog/product/cache/1/image/800x800/9df78eab33525d08d6e5fb8d27136e95/7/_/7.9inch-dsi-lcd-2.jpg">                   | [waveshare/esp_lcd_dsi](https://github.com/waveshareteam/waveshare-ESP32-components/tree/master/display/lcd/esp_lcd_dsi)                 | 🕒      |
| [7inch DSI LCD (E)](https://www.waveshare.com/7inch-dsi-lcd-e.htm) <br/><img style="width: 150px; height: auto; display: block; margin: 0 auto;" src="https://www.waveshare.com/media/catalog/product/cache/1/image/800x800/9df78eab33525d08d6e5fb8d27136e95/7/i/7inch-dsi-lcd-e-2.jpg">                 | [waveshare/esp_lcd_dsi](https://github.com/waveshareteam/waveshare-ESP32-components/tree/master/display/lcd/esp_lcd_dsi)                 | 🕒      |
| [8inch DSI LCD (C)](https://www.waveshare.com/8inch-dsi-lcd-c.htm) <br/><img style="width: 150px; height: auto; display: block; margin: 0 auto;" src="https://www.waveshare.com/media/catalog/product/cache/1/image/800x800/9df78eab33525d08d6e5fb8d27136e95/8/i/8inch-dsi-lcd-c-2.jpg">                 | [waveshare/esp_lcd_dsi](https://github.com/waveshareteam/waveshare-ESP32-components/tree/master/display/lcd/esp_lcd_dsi)                 | 🕒      |
| [10.1inch DSI LCD (C)](https://www.waveshare.com/10.1inch-dsi-lcd-c.htm) <br/><img style="width: 150px; height: auto; display: block; margin: 0 auto;" src="https://www.waveshare.com/media/catalog/product/cache/1/image/800x800/9df78eab33525d08d6e5fb8d27136e95/1/0/10.1inch-dsi-lcd-c-2.jpg">        | [waveshare/esp_lcd_dsi](https://github.com/waveshareteam/waveshare-ESP32-components/tree/master/display/lcd/esp_lcd_dsi)                 | 🕒      |
| [8.8inch DSI LCD](https://www.waveshare.com/8.8inch-dsi-lcd.htm) <br/><img style="width: 150px; height: auto; display: block; margin: 0 auto;" src="https://www.waveshare.com/media/catalog/product/cache/1/image/800x800/9df78eab33525d08d6e5fb8d27136e95/8/_/8.8inch-dsi-lcd-2.jpg">                   | [waveshare/esp_lcd_dsi](https://github.com/waveshareteam/waveshare-ESP32-components/tree/master/display/lcd/esp_lcd_dsi)                 | 🕒      |
| [11.9inch DSI LCD](https://www.waveshare.com/11.9inch-dsi-lcd.htm) <br/><img style="width: 150px; height: auto; display: block; margin: 0 auto;" src="https://www.waveshare.com/media/catalog/product/cache/1/image/800x800/9df78eab33525d08d6e5fb8d27136e95/1/1/11.9inch-dsi-lcd-3.jpg">                | [waveshare/esp_lcd_dsi](https://github.com/waveshareteam/waveshare-ESP32-components/tree/master/display/lcd/esp_lcd_dsi)                 | 🕒      |
| [7-DSI-TOUCH-A](https://www.waveshare.com/7-dsi-touch-a.htm) <br/><img style="width: 150px; height: auto; display: block; margin: 0 auto;" src="https://www.waveshare.com/media/catalog/product/cache/1/image/800x800/9df78eab33525d08d6e5fb8d27136e95/7/-/7-dsi-touch-a-1.jpg"> | [waveshare/esp_lcd_ili9881c](https://github.com/waveshareteam/waveshare-ESP32-components/tree/master/display/lcd/esp_lcd_ili9881c)    | 🕒      |

</details>


## Herdsman LCD dashboard

The existing MIPI display rotation provides a landscape dashboard (1920 x 480
for OTA7290B, 1280 x 800 for JD9365). The top bar shows the transparent brand
logo on the left and the settings button on the right. The clock is right-aligned
to the left of the Wi-Fi, battery, mute, and settings slots, separate from status text. The home content uses
20% / 50% / 30% columns for the audio meter, conversation, and production chart.
The legacy `HERDSMAN_UI_SHOW_LEFT_LOGO` and `HERDSMAN_UI_SHOW_RIGHT_BUTTONS`
switches now control the audio/control column and production chart respectively.

The 33 orange bars keep a fixed waveform silhouette. Their heights scale together
with microphone amplitude, or playback PCM amplitude while speaking. A board-local
ES8311 subclass publishes mean absolute PCM levels through atomics; the LVGL timer
consumes them at 40 Hz with a fast attack and smooth decay. The sound-driven height
variation has 2x visual gain, with a shared limit to preserve the silhouette and
keep it clear of the chat button. This is a decorative
amplitude meter, not a frequency spectrum or calibrated sound-pressure meter.
It allocates no audio buffers and never reads the microphone from the UI thread.

The chart uses seven fixed demo samples labelled “模拟数据”. Feed (blue, 0–1500 kg)
and water (purple, 0–200 L) have scales on the left; eggs (green, 0–2500 pcs) have a
scale on the right. Each series is normalized to its own range before plotting,
so equal heights indicate equal fractions of their ranges, not equal quantities.
The horizontal labels 1–7 represent demo sample numbers. These values are not
connected to Modbus. For operational comparisons, separate stacked charts sharing
a time axis would make the different units easier to interpret.

Regenerate the ARGB8888 logo with its original alpha channel:

```powershell
python scripts/Image_Converter/LVGLImage.py --ofmt C --cf ARGB8888 -o main/boards/waveshare/esp32-p4-nano/assets main/boards/waveshare/esp32-p4-nano/resources/logo.png
```

For this firmware's include layout, use `"lvgl.h"` in the generated C file's
`LV_BUILD_TEST` include branch as well. Board Kconfig selects the chart widget
and Montserrat 14 tick font; no generated sdkconfig should be edited by hand.
Hardware checks: logo transparency, ruler alignment on both LCD sizes, settings
and chat touch targets, microphone/playback response, silence decay, interruption,
and reconnection. A build alone does not validate those behaviors.

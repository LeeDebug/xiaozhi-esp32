# Waveshare ESP32-P4-NANO

## Audio device selection

Set `AUDIO_DEVICE` directly in this board's `config.h` before building:

- `AUDIO_DEVICE_USB_UAC` (default): USB UAC 1.0 microphone and speaker, 48 kHz/16-bit PCM.
- `AUDIO_DEVICE_ES8311`: original onboard microphone and speaker,
  with 24 kHz capture and playback.
- `AUDIO_DEVICE_AP0316_ES8311` (mode 6): AP-0316 input on
  I2S1 as a 48 kHz slave receiver; ES8311 output on I2S0 as a 24 kHz master
  transmitter. See [mode 6 wiring and setup](ap0316-mode6.md).

`config.h` derives `AUDIO_USE_USB_UAC` and `AUDIO_USE_AP0316` from `AUDIO_DEVICE`;
change only `AUDIO_DEVICE` to select a device. The default preserves the
previous USB configuration. All modes publish microphone audio levels to the
Herdsman UI. Onboard mode restores the board-local configuration
and `HerdsmanAudioCodec` wrapper from commit `bc1ada61`, the merge base with
`yishen_v2.4.2__lcd8.8__2.0`. It uses the current shared ES8311 driver, I2C1
(SDA GPIO7 / SCL GPIO8), MCLK GPIO13, WS GPIO10, BCLK GPIO12, DIN GPIO11,
DOUT GPIO9 and PA GPIO53. The same I2C bus is shared with touch, camera and
backlight. USB UAC implementation is excluded by the preprocessor in onboard
and AP-0316 modes, so no UAC USB Host tasks are started. The UAC component
dependency and its build configuration remain available for switching back to USB mode.

This is a build-time choice; edit `config.h` and rebuild to switch devices.
Verify capture, playback and the audio level display with the selected hardware.

The AP-0316 release names are `esp32-p4-nano-10.1-a-ap0316` and
`esp32-p4x-nano-10.1-a-ap0316`, preserving the corresponding chip revision,
display and flash/partition settings of the existing variants. Set
`AUDIO_DEVICE` to `AUDIO_DEVICE_AP0316_ES8311` before using these separate OTA
variant names; the release name does not override `config.h`.

## USB audio

Audio capture and playback use the ESP32-P4 USB Host controller through the
board's Type-A USB 2.0 OTG connector. Connect a USB Audio Class 1.0 sound card
with both a microphone and speaker/headphone output directly to that port.
USB hub support is not enabled in the current firmware configuration.
The Type-C connector is for power, flashing and UART
debugging; the onboard microphone, ES8311 codec, and speaker header are not
used when `AUDIO_USE_USB_UAC=1`.

The UAC adapter accepts 48 kHz, 16-bit PCM with one or two channels in each
direction. It downmixes stereo microphone audio to mono and duplicates mono
playback to a stereo USB speaker. The firmware audio service performs its
normal 48 kHz to 16 kHz microphone resampling. A device without a matching
UAC 1.0 mode is logged and ignored. USB audio interfaces can be connected,
removed, and reconnected while the application runs.
UAC 2.0 devices (descriptor version `0x0200`) are detected before the UAC 1.0
host driver opens them and are skipped. To use this firmware, select a UAC 1.0
mode on the sound card if it provides one, or use a UAC 1.0 sound card.

Both release variants set `CONFIG_UAC_FREQ_NUM_MAX=16`. The board's Kconfig
constraint also raises an older saved value of 4 to at least 16 during the next
configuration step. Use the canonical `scripts/build.py` variant entry point
when building. No generated `sdkconfig` files need to be edited by hand.
Some UAC 1.0 microphones advertise six discrete rates with 48 kHz after the
fourth entry; the driver's default capacity of four hides that mode. The adapter
rejects truncated rate lists before starting the driver, since usb_host_uac 1.x
also uses the untruncated count when selecting a stream. If a device advertises
more than 16 rates, increase the option to at least the count in the error log
(up to 82). Each alternate setting now logs its PCM format and 48 kHz support
to distinguish an undersized host rate list from an unsupported device format.

Playback waits for the control task to finish resuming an enabled USB stream
(with a bounded timeout), so the first audio buffer after idle is not silently
discarded while the resume event is still queued. A working startup sound alone
does not verify the conversation path: microphone initialization must succeed
before voice activation and speech upload can produce a spoken reply.

After flashing, verify microphone capture, playback, voice activation,
interruption, reconnect, and simultaneous capture/playback with the intended
USB sound card. Also check that the Type-A port supplies the
sound card's required current. Firmware builds cannot validate USB device
descriptors or power delivery.


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

The 33 orange bars are drawn by `fb_gfx` into one fixed LVGL RGB565 canvas,
not separate LVGL objects. Its aligned buffer is allocated once (PSRAM preferred,
8-bit memory fallback), reused for all frames, and freed with the display.
The adapter accounts for LVGL row stride and the component's high-byte-first
RGB565 writes. Each update clears and draws only this buffer, then invalidates
one canvas area, avoiding per-bar layout changes and dirty-region overflow.
The `fb_gfx` component declares `esp_libc` on ESP-IDF 6 and `newlib` on
ESP-IDF 5 directly in its CMakeLists.txt. No project-side compatibility component
is needed.

The bars keep a fixed waveform silhouette. Their heights scale together
with microphone amplitude only in the listening state. Speaking and all other
states immediately reset to the minimum (quiet) silhouette without a decay tail;
playback PCM never animates the meter. A board-local USB UAC codec publishes mean
absolute PCM levels through atomics; the LVGL timer consumes them at 40 Hz with a
fast attack and smooth decay while listening. The sound-driven height variation
uses a 4x gain, twice the previous 2x gain, while the quiet silhouette stays
unchanged. A shared limit preserves the silhouette and keeps it clear of the
chat button. This is a decorative
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
`LV_BUILD_TEST` include branch as well. Board Kconfig selects the canvas and chart widgets
and Montserrat 14 tick font; no generated sdkconfig should be edited by hand.
Hardware checks: logo transparency, ruler alignment on both LCD sizes, settings
and chat touch targets, listening response, doubled excursion, minimum bars while speaking or idle,
silence decay, interruption,
and reconnection. A build alone does not validate those behaviors.

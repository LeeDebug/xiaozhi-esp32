# Waveshare ESP32-P4-NANO

## Ethernet and Wi-Fi

Release variants default to `CONFIG_XIAOZHI_NETWORK_ETHERNET=y`, which enables
Ethernet priority with Wi-Fi backup. The network worker allows Ethernet 10 seconds
from network startup to establish a link **and obtain an IPv4 address**. Saved
Wi-Fi credentials are tried in parallel. After the Ethernet deadline, Wi-Fi is
used if Ethernet is unavailable; missing credentials enter provisioning immediately,
and an unsuccessful primary Wi-Fi connection enters provisioning after 60 seconds.
A failed backup Wi-Fi connection does not interrupt working Ethernet.

Ethernet is selected as the default route whenever ready, including after recovery.
Wi-Fi stays connected as backup. A route change reports a disconnect/reconnect so
an active conversation can close its old connection; existing TCP sessions are not
migrated between interfaces. A provisioning portal remains open until credentials
are submitted, even if Ethernet becomes ready meanwhile. BOOT during startup also
opens provisioning. The existing Wi-Fi-only menuconfig option remains available.

Hardware validation: boot with/without cable, link without DHCP, saved valid/invalid
Wi-Fi credentials, empty credentials, Wi-Fi timeout and portal submission, unplug
and replug Ethernet with Wi-Fi connected, and interrupt backup Wi-Fi while Ethernet
is active. Check default route, DHCP/DNS and reconnect for both WebSocket and MQTT/UDP.

## Audio device selection

Set `AUDIO_DEVICE` directly in this board's `config.h` before building:

- `AUDIO_DEVICE_USB_UAC`: USB UAC 1.0 microphone and speaker, 48 kHz/16-bit PCM.
- `AUDIO_DEVICE_ES8311`: original onboard microphone and speaker,
  with 24 kHz capture and playback.
- `AUDIO_DEVICE_AP0316_ES8311` (mode 6): AP-0316 input on
  I2S1 as a 48 kHz slave receiver; ES8311 output on I2S0 as a 24 kHz master
  transmitter. See [mode 6 wiring and setup](ap0316-mode6.md).

`config.h` derives `AUDIO_USE_USB_UAC` and `AUDIO_USE_AP0316` from `AUDIO_DEVICE`;
change only `AUDIO_DEVICE` to select a device. The current selection is
`AUDIO_DEVICE_AP0316_ES8311`; the code does not override that selection. All modes publish microphone audio levels to the
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

## AP-0316 hardware AEC and voice interruption

Both `AUDIO_DEVICE_USB_UAC` (AP-0316 mode 1) and
`AUDIO_DEVICE_AP0316_ES8311` (mode 6) can interrupt the assistant's speech
by speaking normally. The AP performs hardware echo cancellation; the P4
continues microphone capture/VAD while playing and sends a protocol `abort`
when confirmed speech interrupts the current answer. The server must accept
realtime listening and `abort` and continue processing the microphone stream.
The existing `CONFIG_USE_AUDIO_PROCESSOR=y` feature is required for local VAD;
no new menuconfig option is introduced by this board feature.
The ordinary ES8311 mode is unaffected. After TTS stop, a bounded quiet
interval allows late audio packets (especially MQTT/UDP) to reach the playback
queue before returning to listening; playback must also finish draining.
The protocol has no response ID: delayed old TTS start/stop events, or an old UDP
audio packet arriving after a new TTS start, cannot always be distinguished from
the new response. The finite drain interval reduces tail reordering problems
but does not provide absolute isolation between network responses.

Configure these switches directly in `config.h`:

| Definition | Default | Meaning |
| --- | --- | --- |
| `USB_UAC_HAS_HARDWARE_AEC` | `1` | USB device is AP-0316 with working hardware AEC; set `0` for an ordinary USB sound card. |
| `AP0316_HAS_HARDWARE_AEC` | `1` | Mode 6 has a connected and working LINE_IN playback reference. |
| `AUDIO_VOICE_INTERRUPTION_ENABLED` | `1` | Enable local VAD speech interruption when hardware AEC is available. |
| `AUDIO_VOICE_INTERRUPTION_MIN_SPEECH_MS` | `200` | Minimum continuous speech used to confirm interruption. |
| `AUDIO_VOICE_INTERRUPTION_PLAYBACK_GUARD_MS` | `300` | Ignore interruption near the start of playback while AEC settles. |
| `AUDIO_VOICE_INTERRUPTION_PLAYBACK_DRAIN_MS` | `180` | Limited quiet interval after TTS stop and the last audio packet, before finishing drained playback. |

AP-0316 already returns echo-cancelled mono audio. Leave its independent
reference-channel flag false; `CONFIG_USE_DEVICE_AEC` is not required for
these hardware AEC paths. That option describes software AEC on the P4.
Disabling the hardware AEC switch restores the normal non-hardware-AEC path;
turning off the local interruption switch disables the local VAD abort policy.

For mode 1, connect P4 USB Host to AP J1, microphone to AP J3 and speaker to
AP J2. Playback must pass through the AP so it can derive its internal USB
reference; ES8311 and external LINE_IN are not used for this mode.
Mode 6 needs the ES8311 analog playback reference wired to AP LINE_IN as
described below. Input and output sample rates may differ because AP and DAC
use independent I2S controllers and clocks.

USB capture and playback now use separate buffers/locks. Cancellation marks a
playback generation immediately, then the USB control task suspends/flushes the
output interface before resuming a new answer. Capture remains active. Mode 6
mutes the PA immediately and replaces TX DMA contents with silence before the
next generation. Old queued/in-progress playback generations cannot resume.
Hardware and USB device buffering can still add a short physical stop tail.

Physical validation is required: playback without speaking must not self-trigger;
speak while playing and confirm the first syllable reaches the server, playback
stops, and the next answer plays. Repeat interruptions, idle/new conversations,
USB removal/reconnection and mute/volume changes. Test at the actual speaker
volume and microphone distance, then adjust confirmation/guard times for residual
echo. Verify both transports against the deployed server's realtime/abort support.

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

The tool set follows `docs/big-herdsman-docs/AI 语音管家‑生产监控终端 PMC Modbus‑RTU 完整指令集.md` and `docs/big-herdsman-docs/AI语音管家与生产监控终端通讯控制逻辑表.xlsx`. It covers holding registers 0x00-0xA6 in functional groups (all names use the `self.production.` prefix):

| Group | Tools | Registers |
| --- | --- | --- |
| Device | `device.get_info`, `device.address.set`, `modules.get`, `log.get` | 0x00-0x01, 0xA6, 0x6A |
| Silos | `silos.get_info`, `silos.get_statistics` | 0x02-0x31 |
| Water meters | `water_meters.get` | 0x32-0x3A |
| Lights | `light.get`, `lights.get`, `light_1..4.open/close`, `lights.set_all`, `light_schedule.get/set` | 0x3B-0x3E, 0x8D-0x94 |
| Power | `power.get` | 0x3F-0x46 |
| System status | `system_status.get`, `daily_mortality.set` | 0x47-0x4D |
| Alarms | `alarms.get`, `alarm_pause.set` | 0x4E-0x66, 0x95 |
| Water valve / temperature | `main_water_valve.get/set`, `water_temperature.get` | 0x6B-0x6C |
| Dosing | `dosing.get`, `dosing.set` | 0x6D |
| Flushing | `flush.get`, `flush.start`, `flush.stop`, `flush_schedule.set`, `flush_config.set` | 0x6E-0x7A |
| Feed carts | `feed_cart.get`, `feed_cart.start`, `feed_cart.start_time.set` | 0x7B-0x82 |
| Feed lines | `feed_lines.get`, `feed_lines.set`, `feed_line_mode.set`, `derailment.get` | 0x83-0x8C |
| Eggs | `eggs.get` | 0x96-0xA5 (+0x47 for laying rate) |

Notes on behaviour and protocol interpretation:

- Reading a functional group uses one function-code 0x03 request for the whole contiguous block (for example 48 registers for all four silos, 25 registers for all alarms). 32-bit values are assembled low word first; water-meter values use a 0.01 fixed-point scale and water temperature / ratios use 0.001. Neither is IEEE754.
- All writes use function-code 0x06. Bitfield registers (0x6D, 0x6E, 0x7B/0x7D/0x7F, 0x95) are read-modify-written so read-only high bytes and unrelated bits are preserved. Flush start, flush scheduling and feed cart start first read the relevant state/mode registers and refuse to act when the equipment is busy or in emergency mode. `device.address.set` also updates the local master address and re-reads 0x01 with the new ID.
- `lights.set_all` writes 0x3B-0x3E sequentially with four 0x06 requests and reads them back. The protocol document's single-frame 0x10 batch write is **not** available because `ProductionModbus` does not implement function code 0x10; the four writes are not atomic.
- Two documents disagree on bit ordering for 0x87/0x88 (the instruction set says bit0 is line 1; the logic table says "from high to low"). The tools follow the instruction set and always return `raw_value` so the mapping can be checked against real hardware. The logic table also shows `27 3B` as the encoded form of 23:59 while the tools use `(hour << 8) | minute` (`17 3B`); verify this encoding on hardware before relying on `light_schedule.set`.
- Register 0x4D ("time", write-only) is read by `system_status.get` but has no write tool because the protocol does not define its encoding. The voice scenario "schedule a flush 30 minutes from now" is exposed as an explicit week/hour/minute triple; the caller must convert relative time to an absolute schedule.
- Laying rate is computed as `egg_total / chicken_count * 100`. Confirm whether 0x96 is a daily or cumulative total before using that ratio operationally.
- `silos.get_info`, `main_water_valve.*`, `water_temperature.get` and the original light tools keep their previous semantics. `light.get` now accepts channels 1-4 and `lights.get` returns four entries instead of two. Valve writes still use protocol value 1 for open and 2 for closed.

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
to the left of the Wi-Fi, Ethernet, battery, mute, and settings slots, separate from status text.
Wi-Fi and Ethernet indicators refresh independently on every status-bar tick (including
backup-interface changes). Wi-Fi shows green signal bars when connected and a gray
off icon when disconnected. The RJ45 Ethernet icon is green with a usable IPv4
address, amber while the link is up awaiting an address, and gray with a diagonal
slash when disconnected or disabled in a Wi-Fi-only build. These indicators show
local interface readiness, not server reachability or which interface is primary. The home content uses
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

The top bar, conversation area, and recommended-question panel use the page
background color. Rounded message bubbles use the theme's user, assistant, and
system bubble colors to distinguish messages. Startup board/version metadata is hidden from
the conversation; other system messages remain visible.

The chart uses seven fixed demo samples, with no demo caption or displayed units. Feed (blue, 0–1500 kg)
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

Chat avatars use `resources/ai.png` for assistant messages and `resources/me.png`
for user messages. Keep both PNGs at 54 × 54 pixels with an alpha channel; they
are displayed at native size inside circular containers. Regenerate their
ARGB8888 assets after replacing the PNGs:

```sh
python scripts/Image_Converter/LVGLImage.py --ofmt C --cf ARGB8888 -o main/boards/waveshare/esp32-p4-nano/assets main/boards/waveshare/esp32-p4-nano/resources/ai.png
python scripts/Image_Converter/LVGLImage.py --ofmt C --cf ARGB8888 -o main/boards/waveshare/esp32-p4-nano/assets main/boards/waveshare/esp32-p4-nano/resources/me.png
```

Use `"lvgl.h"` in the generated avatars' `LV_BUILD_TEST` include branch too.
Check avatar transparency, circular clipping, and left/right message alignment
on the LCD after flashing.

Hardware checks: logo transparency, ruler alignment on both LCD sizes, settings
and chat touch targets, listening response, doubled excursion, minimum bars while speaking or idle,
silence decay, interruption,
and reconnection. A build alone does not validate those behaviors.

## P4X OTA staging

The `esp32-p4x-nano-10.1-a` variant downloads the complete image to a 5 MiB
PSRAM buffer before starting flash writes. An internal 4 KiB buffer is used for
network reads and flash writes. A missing/oversized HTTP body, incomplete download,
invalid image header, or failed allocation aborts without erasing the OTA partition.
The variant keeps PSRAM XiP disabled and FreeRTOS in IRAM; OTA previously worked
with that combination, although the display flickered while flash was written.
The download phase reports 0-100% in the status bar. After the full image is
received, the UI shows “正在写入，完成后将自动重启”, waits 1.5 seconds,
then fades the backlight off and waits 0.6 seconds before the first flash write.
If writing fails, the previous brightness is restored. Staging separates download
from flashing; the screen is deliberately dark during flash writes because display
refresh can flicker while flash is busy.
A contiguous 5 MiB free PSRAM block is required. Validate on hardware with Ethernet
and Wi-Fi, interrupted downloads, OTA boot, display, and audio.

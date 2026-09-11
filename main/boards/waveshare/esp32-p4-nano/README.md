# Waveshare ESP32-P4-NANO


[ESP32-P4-NANO](https://www.waveshare.com/esp32-p4-nano.htm) is a small size and highly integrated development board designed by waveshare electronics based on ESP32-P4 chip

## Production-monitoring Modbus

This board starts an RTU master on UART1 (TX GPIO21, RX GPIO22) at 9600 baud, 8 data bits, no parity, and 1 stop bit. The attached production-monitor protocol is cached as four holding-register ranges:

- Alarm registers 78-102 are read every second.
- Water-control registers 0x67-0x68 are read every second; reserved register 0x69 is skipped.
- General registers 0-77 and system/control registers 106-166 are read every five cycles.
- MCP tool `self.production.main_water_valve.set` opens/closes the valve through register 0x67, and `self.production.water_temperature.get` returns the cached register 0x68 value as Celsius using a 0.001 scale.
- Valve writes use protocol value 1 for open and 2 for closed.

The device address defaults to 0, which means not configured; Modbus polling and writes are disabled until `ProductionModbus::SetDeviceAddress(1..247)` is called. Cached values are available through `GetCachedRegister()` and `GetSnapshot()`. Device writes use `WriteHoldingRegister()` and are serialized with polling by a shared bus mutex.

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
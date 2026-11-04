# Project context

XiaoZhi ESP-IDF firmware. Follow root AGENTS.md; use Board/AudioCodec interfaces
for capabilities, keep board pins/configuration local, preserve generated/vendor
files and unrelated changes. User explicitly requests no firmware compilation.

Current board: main/boards/waveshare/esp32-p4-nano. Audio selection is defined
directly in config.h (USB UAC, onboard ES8311, or AP-0316 I2S input + ES8311 output).
User has verified AP-0316 USB compatibility. USB mode uses AP mode 1, with mic and
speaker on AP; I2S mode uses AP mode 6 and requires analog LINE_IN playback reference.

AP-0316 performs external hardware AEC. The host receives cleaned mono PCM with
input_reference=false; do not fabricate a second reference channel or enable
software AEC for it. Existing core supports realtime listening and protocol abort.

## Modbus Web simulator (2026-11-03)

Local-only browser tool in main/boards/waveshare/esp32-p4-nano/web; see its README
for source analysis, register map and launch instructions. Firmware is unchanged.
Uses current executable register constants 0x6B/0x6C despite stale 103/104 comment.
Web Serial 03/06 slave, editable 167 registers, persisted settings, offline injection.

2026-11-04 update: RTU requests are now validated only after a receive-idle
boundary; no fixed-length early emission or CRC substring scanning. See
web/framing.mjs, strict RtuDecoder, and web README timing limitations. Default
9600-8N1 gap 4ms, configurable while disconnected. R2 tests: 35/35 pass.

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

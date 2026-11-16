# Parent acceptance — 2026-11-03

- PASS: node --test main/boards/waveshare/esp32-p4-nano/web/*.test.mjs: 22/22.
- PASS: protocol, serial, app and test module node --check.
- PASS: HTTP 200 index.html/style.css/app.mjs/serial.mjs/protocol.mjs; JS MIME text/javascript.
- PASS: browser UI inspected; default and configured reads, valve echo, negative
  int32 low-word-first, persistent settings, register address filtering, bad CRC
  without TX. No console warning/errors. Screenshot preview.png.
- PASS: independently inspected queue/locks/cleanup; corrected delay editing
  throwing inside receive callback and error status being hidden by disconnect.
- PASS: git diff --check, no firmware or generated sources changed.
- NOT_RUN: firmware build and physical serial/device validation; no hardware used.

Only board/web, board README and coordination/evidence files changed. No external
hosting or deployment. New tool has no install step or CDN dependency. Data and
logs stay in the browser unless exported. Protocol timing limitations and precise
wiring/launch/physical acceptance instructions are in web/README.md.

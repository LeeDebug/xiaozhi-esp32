# WP-ap0316-barge-in / R1

## Objective and baseline

Implement local speech interruption with AP-0316 hardware AEC in USB UAC and
AP-0316 I2S input + ES8311 output modes. Keep all feature controls in board config.h.
Baseline HEAD: ad0631c. Worktree was clean at start of this task. Preserve the
user's current AUDIO_DEVICE_AP0316_ES8311 selection and GPIO assignments.

## Authorized validation

User confirmed standard execution and static checks + host targeted regression.
Workflow and validation questions were presented per single-conversation-subagent skill.
User's standing instruction: no firmware compile. No flashing, serial access,
long stability runs, network messages, or edits to sdkconfig/vendor/generated files.

## Work packages

- core: AudioCodec capability contract, application realtime/VAD interruption,
  AudioService output cancellation and AFE integration. Own core/audio files only.
- board: board config flags, capabilities on both AP codecs, independent USB
  capture/playback I/O and bounded cancellation; board docs. Own board files only.
- review: after implementation, independent read-only review and host tests.

Contract to coordinate: HasHardwareAec(), GetVoiceInterruptionConfig() (enabled,
min_speech_ms, playback_guard_ms, playback_drain_ms), generation-aware
OutputData/CancelOutput/ResumeOutput. Defaults must leave
other boards unchanged. Agents may refine cancellation contract together and
report changes to coordinator. No child delegation.

## Acceptance

Playback keeps input/VAD active for enabled hardware AEC modes. Sustained speech
triggers one main-task interruption, clears stale output and notifies server via
shared Protocol API without discarding ongoing uplink. Software AEC is bypassed
for external hardware AEC. Short noise/stale VAD/late audio and reconnect paths
are guarded. USB input/output don't serialize behind one long I/O mutex. Disabled
feature and ordinary ES8311 behavior remain unchanged.

## Status

Implementation and independent static acceptance complete. Agent IDs: /root/core_barge_in (core), /root/board_audio_io
(board), /root/review_barge_in (read-only audit and host validation).
Evidence: docs/agent/evidence/WP-ap0316-barge-in/R1/.
Core also owns the necessary default callback extension in audio_engine.h.
Review focus: per-frame VAD confirmation (not edge-only), stale capture reset,
output generation/in-flight cancellation, TTS admission before first packet,
early TTS stop with buffered audio, reconnect invalidation, and keeping uplink
queues intact on interruption. Protocol lacks response IDs; no claim of perfect
cross-response reordering detection is permitted.
No source-string-only or duplicate-algorithm tests are to be introduced.
Firmware build and physical/acoustic validation: NOT_RUN by user instruction / no hardware.

## Final acceptance evidence

- Core and both board codec implementations: complete; current AUDIO_DEVICE and
  GPIO assignments preserved. Feature controls remain in board config.h.
- Independent static review: PASS, including final output reset recovery,
  admission cancellation and decoder history changes; see review/final-review.md.
- Parent git diff --check: PASS. Parent clang-format dry-run on both board
  codec pairs and the new voice_interruption.h: PASS. Core worker checked
  formatting on changed lines of existing core files.
- Existing host regression: 62 tests, 61 pass; one identical baseline failure
  in generated assets/logo__qingdao_tsg.c:5 referring to ../lvgl.h. No new host
  failures; see review/host-baseline.log and review/host-final.log.
- No firmware/C++ build, flashing or acoustic/device tests performed. Host tests
  do not execute the new audio task concurrency paths. Physical acceptance must
  cover full duplex, playback-only false triggers, first-word preservation,
  repeated interruptions/new replies, both transports and USB reconnect.
- No sdkconfig, generated or vendor files changed. Protocol wire contracts
  unchanged; old markers/UDP packets arriving after a new response remain
  ambiguous without server response IDs.

## WP-web-modbus / R1 — 2026-11-03

User requested analysis of board Modbus and a Web Serial slave in board/web.
User selected standard execution + quick smoke and focused regression. Scope:
local static web files, board README link and coordination/evidence; no firmware
build, flashing, real serial access, hosting, dependencies or generated changes.
Baseline worktree clean. Parent owns UI/serial/docs and acceptance; execution
agent /root/modbus_engine owns protocol engine/tests and serial tests, no delegation.

PASS: parent executed all 22 Node tests (13 RTU protocol + 9 Web Streams serial
lifecycle); syntax checks; all five browser assets HTTP 200 with JS MIME correct;
git diff --check. Browser CUA validation: 167 register rows, default 25.500°C
returns 25500; modified 32.126°C returns 32126; -99kg returns low/high words
65437/65535; 06 valve write echoes and updates state; lights returns [60,80];
CRC-invalid request generates no TX; refresh retains edits; address filter works;
no console warn/error captured. Browser fill API needed a real key edit/blur to
commit numeric changes; validated actual keyboard change event. Restored example
data afterward. Screenshot review/preview.png. Node tests are fake ports, not
physical Web Serial acceptance. Independent review identified live invalid delay
throw and overwritten disconnect error; parent fixed both (committed delay state,
retain error notice). Parent inspected protocol/serial implementation and tests.

NOT_RUN: physical RS485/UART1 wiring, USB adapter direction/echo, device MCP
roundtrip, unplug/reconnect with actual serial device, firmware builds. Browser
RTU idle boundary is approximate (30ms), known 03/06 frames parse immediately.
Local preview server is stopped during final teardown. Evidence:
docs/agent/evidence/WP-web-modbus/R1/{engine,review}/.

## WP-web-modbus / R2 — 2026-11-04

User follow-up: split Modbus by frame gap to avoid false interpretation. Same
accepted workflow/validation scope. Baseline includes the R1 work and a user's
untracked board/web.zip, which is preserved and not regenerated.

Implemented strict whole-interval decoding: push only accumulates; flush is called
only after receive silence. No fixed 8-byte early parsing or CRC subframe search.
Reject entire noisy/concatenated/oversized interval, with bounded first-256-byte
diagnostic retention and overflow state until the next boundary. Parent added
GapFramer with monotonic clock and early/late/stale timer handling; app routes
all serial input through it, resets on disconnect/read errors, and guards against
late read callbacks after close. Configurable frame gap is locked online, with
8N1 defaults 9600=4ms, 19200=2ms, >19200=2ms; manual value 1..1000ms. README
updated to explain browser chunk timestamps and conservative discards.

PASS: parent full 35 tests (18 protocol, 8 fake-clock framing, 9 Web Streams serial),
syntax checks, git diff --check. Execution agent /root/modbus_engine implemented
protocol + tests and independently reviewed framing/app/timer integration, 35 PASS.
Browser: frame-gap UI defaults4, baud change19200=>2,9600=>4, manual key edit=>5,
normal offline water read still25500, reload loads all modules, no console warnings
or errors. Screenshot R2/review/preview.png. Existing live user Chrome serial page
was not manipulated; verification used separate local IAB and no physical port.
NOT_RUN: real serial timing/device tests, firmware compile/flash. Local QA server
stopped on completion. Evidence: docs/agent/evidence/WP-web-modbus/R2/.

## WP-data-report / R1 — 2026-11-11

User request: add production data reporting per 《设备端接入指南》 with base URL
http://120.53.1.196:8002, using the Excel register map. User chose docx catalog as
authority (not the live 28-key config), standard execution, smoke+targeted scope.
Mid-task the user instructed: "只改代码，不做编译和环境修改". From then on no builds,
no IDF env changes (a prior attempt to install IDF py deps in ~/.espressif was denied
and must not be retried).

Implemented (board-local, waveshare/esp32-p4-nano):
- production_data_report.{h,cc}: dedicated "data_report" task; GET
  /xiaozhi/data-report-config/current after network connect, SNTP time sync with
  uptime fallback, chunked Modbus 0x03 reads (64-register chunks), cJSON upload POST
  to /xiaozhi/data-analysis/upload with deviceInfo/location toggles, retry/backoff,
  pause on disconnect. Multi-word fields decode low word first (matches MCP tools).
- production_data_report_fields.inc: 152/152 docx catalog entries, keys+addresses
  verified equal to docx table by script; 10 bit fields for 0xA6.
- ethernet_wifi_board.h: class now also derives ProductionBoardLinkage, adds
  SetNetworkStateObserver() and IsNetworkLinked(); observer called next to the App
  callback in Notify().
- esp32-p4-nano.cc: AttachBoardLinkage + observer + InitializeProductionDataReport()
  under CONFIG_XIAOZHI_NETWORK_ETHERNET.

Verified: docx/Excel parsed and server endpoints probed (GET 200, POST accepted);
field catalog cross-checked 152/152 vs docx; board diff reviewed; residual compile
defects from two earlier ad-hoc -fsyntax-only runs were fixed by inspection
(kNoKeysRecheckTicks, sntp callback signature timeval*, GetEpochMillis constness).

NOT_RUN: any firmware build, flash, physical RS485/HTTP roundtrip, clang-format.
Known risk: no compiler confirmation since the last edits.

## WP-data-report / R2 — 2026-11-11

User: 在代码里打印和数据服务器交换的相关信息; chose detailed exchange logging.
Still code-only per user instruction (no build, no env changes).

Added in production_data_report.cc:
- LogHttpExchange(): logs ">>> [phase] METHOD url", request body with byte size,
  "<<< [phase] status=N, bytes, elapsed=ms", full response body (or empty warning);
  used by both config GET and upload POST. Truncation of responses now warns.
- LogConfigSummary(): enabled/interval(min+s)/deviceInfo/location/keys/skipped; plus
  first 8 keys list.
- Report attempt logging: "Report #N: reading ... Modbus address ...", payload built
  with field count/bytes, per-chunk Modbus 0x03 range at DEBUG, chunk+register totals,
  "Report #N done: ... next report in ... s", failure/retry logs include N.
- kMaxResponseBytes 16KB -> 32KB so full responses are logged without loss.
- Separate counters: config_loaded_count_ (config generation) and report_attempts_.

NOT_RUN: build/format/hardware. No compiler confirmation after these edits.

## WP-web-simulator / R3 — 2026-11-11

User: 完善 web-serial 模拟器所需的全部模拟数据（依据 152 项上报字段与寄存器表）。
Chosen: 补全默认模拟值 + 保留可编辑; 验证 = 本地 Node 测试 + 字段一致性校验.

Implemented:
- web/registers.mjs (new): REGISTER_CATALOG (152 entries incl. 10 bit fields), BUSINESS_FIELDS
  (28 physical-quantity inputs), PRESET_VALUES/createPresetRegisters() (167-register snapshot),
  RESERVED_REGISTERS, splitWords/joinWords/writeField/writeRaw/readField, describeRegister,
  formatRegister/formatBits.
- web/registers.test.mjs (new, 13 tests): 152 keys match firmware .inc (name+address), reserved
  addresses stay 0, alarm region clear, enum ranges, low-word-first, egg column sum equals total,
  64-register chunked serial roundtrip equals preset.
- web/app.mjs: uses the module (names, preset, generated value form, decoded column in table),
  storage key v2; removed old fixed silo/valve/temperature/light inputs.
- web/index.html: #fields container replaces fixed inputs.
- web/README.md: catalog/preset documentation, test count.

PASS: node --test web/*.test.mjs = 48/48 (35 existing + 13 new); node --check on all .mjs;
headless Chrome render: 28 field inputs, 167 rows, decoded meanings (25.500, 12000, 12.340,
1.850, 0xA6 bit expansion); read frames match docs (version reply 01 03 02 04 52 3B 79 = 1106,
water temp 25500, lights 60/80, silo1 12000, 0xA6 0x03F8). Temp server on :8766 stopped.

NOT_RUN: Web Serial with physical adapter, firmware build/flash, real device roundtrip.

## WP-data-report / R4 — 2026-11-11 崩溃分析

异常: assert "xQueueSemaphoreTake queue.c:1713 (pxQueue->uxItemSize == 0)" at
freertos_hooks.c:31 (fpPanic at ISR->assert). Path: EspTcp::ReceiveTask ->
DoDisconnect(false) -> disconnect_callback_ -> HttpClient::OnTcpDisconnected ->
lock_guard(mutex_) while receive task priority 1 already inherits priority from
http_client taking mutex_ in OnTcpData; task waiting on mutex_ is owner of
ESP_TCP rx? Root cause: EspTcp uses close(2) on a UDP-protocol socket
(xQueueSemaphoreTake on a UDP queue is triggered by lwIP processed/af_pending
release), diagnosed as lwIP UDP socket queue assert/FSM issue; supplier lib
close(2) misuse. See evidence file R4.

Status: NO fix applied yet (analysis only; user has not requested a code change).
Candidate mitigations (board-local, no vendor edits): one in-flight HTTP job at a
time in production_data_report.cc; avoid reusing a client across the 10s receive
task window; reduce concurrent TCP users. Real fix belongs to 78/esp-ml307
(EspTcp::DoDisconnect must not call close(2) from its own receive task and must
not free the socket while the receive task is still running) and needs user
consent to edit managed_components.

NOT_RUN: reproduction on hardware; vendor lib not modified per AGENTS.md.

## WP-ml307-decouple / R5 — 2026-11-11

User: 当前板 main/boards/waveshare/esp32-p4-nano 找不到/去掉与 ML307 的关联。Mode: 标准执行
(定位+修复), 定位阶段只调查。用户要求不编译、不改环境。

Findings inside the board dir: zero textual ML307 references. All coupling is indirect:
1) production_data_report.cc had `#include "network_interface.h"` (vendor header) - removed;
   ReadResponse is now a template, call sites pass `*http` and Close() explicitly.
2) production_data_report.h had `#include "board.h"` (pulls vendor http.h/web_socket.h/...) and never
   used it - removed.
Remaining (outside board dir, shared infrastructure, NOT ML307-modem specific):
- boards/common/board.h includes vendor <http.h>,<web_socket.h>,<mqtt.h>,<udp.h>,<network_interface.h>.
- boards/common/ethernet_board.cc includes <esp_network.h> and uses `static EspNetwork network;`
  -> this IS the live network stack of this board (http_client/esp_tcp); the crash path.
- main/CMakeLists.txt:51 builds boards/common/ml307_board.cc unconditionally.
- main/idf_component.yml:32 `78/esp-ml307: ~3.6.6` has no rules => applies to all boards.

Behavior change (deliberate): HTTP client is now closed explicitly after reading the response, and
no longer destroyed inside ReadResponse, which matches the crash analysis (二次断开回调).

NOT_RUN: build, hardware. Static only (brace balance, call-site review).

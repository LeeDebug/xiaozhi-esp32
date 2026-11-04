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

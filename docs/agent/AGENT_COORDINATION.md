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

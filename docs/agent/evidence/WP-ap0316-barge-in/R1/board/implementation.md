# Board audio implementation evidence

Scope: esp32-p4-nano board codecs, config and documentation only. No compile,
flash, serial access, vendor/generated edits or pin changes. Existing
AUDIO_DEVICE_AP0316_ES8311 selection retained.

## Interface

Uses core AudioCodec VoiceInterruptionConfig, HasHardwareAec(),
GetVoiceInterruptionConfig(), CancelOutput(generation), ResumeOutput(generation),
OutputData(data, generation). Config defaults hardware AEC and local voice
interruption on for USB/AP0316 paths; ordinary ES8311 has no overrides.
Generation matching is checked at entry and each output chunk, so an old task
which enters after a cancel/resume is rejected.

USB input/output own separate buffers and direction I/O mutexes. Stream handles
cannot be closed or suspended during their direction's I/O. Global state mutex
is released during USB read/write and slow suspend/resume/open/close calls.
Cancel only changes atomics, notifies waiters and queues a nonblocking update;
control task suspends and flushes the output ring before new playback. Resume
wait releases all I/O locks. Input ring retains 16384 bytes; output ring is
3840 bytes (40 ms stereo / 80 ms mono), writes use 480 frames and 20 ms timeout.

AP cancellation immediately mutes the PA via GPIO, retains RX, and invalidates
old generation writes. Each output I/O is <=240 frames with 20 ms timeout. New
playback clears all TX DMA descriptors using disable/preload-silence/enable in
the output task. Cancel and resume do not wait for an I2S/control operation.
GPIO writes are followed by cancellation rechecks to cover PA-enable races.

## Source checks

- clang-format --dry-run -Werror: both board codec .h/.cc files passed.
- git diff --check -- main/boards/waveshare/esp32-p4-nano: passed.
- Read-only vendor uac_host.c: uac_host_interface_suspend halts/flushes endpoint
  and calls _ring_buffer_flush; no managed component edited.
- Read-only IDF v6.0 i2s_common.c/header: disable resets descriptor pointer/queue;
  preload supports READY state and reports bytes loaded; get_info supplies total
  DMA bytes. Failed reset can retry from already-disabled READY state.

Physical validation remains required for AP mode1 internal reference and mode6
LINE_IN reference, echo-only false positives, concurrent playback/capture,
first syllable preservation, repeated interruptions/new-answer recovery,
volume/mute, USB unplug/reconnect and deployed server realtime/abort handling.

## TTS tail drain parameter follow-up

Added AUDIO_VOICE_INTERRUPTION_PLAYBACK_DRAIN_MS=180 in config.h (range
0..2000 ms), returned as the fourth VoiceInterruptionConfig field by both
codecs. README/mode6 docs describe finite quiet allowance after TTS stop/last
audio packet plus playback drain, covering MQTT/UDP late tail packets. No core
files changed by this board work. Re-ran codec format and board diff checks.

## AP partial-preload recovery follow-up

Reset failure now attempts to re-enable TX while the PA remains muted. If a
subsequent reset encounters READY, it performs a successful enable/disable
cycle before preloading again. This uses public APIs to reset partial/full DMA
preload cursors and avoids repeatedly appending to a failed preload. Input RX
remains running. Rechecked clang-format and board diff whitespace; no compile.

EnableOutput also requires no pending output flush before enabling the PA, so
a stale popped task calling EnableOutput after Cancel/Resume cannot expose old
DMA audio. The valid new-generation Write performs the muted flush first.

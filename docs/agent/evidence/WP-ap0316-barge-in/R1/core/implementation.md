# Core implementation evidence

Scope: Application, AudioCodec/AudioEngine interfaces, AudioService, AFE, new voice_interruption.h. No board/vendor/generated/sdkconfig edits by this work package. No C/C++ compilation, firmware build, or flash performed.

## Behavior

- Capability opt-in requires codec hardware AEC, board interruption enabled, and CONFIG_USE_AUDIO_PROCESSOR on an AFE target. Otherwise original listening selection applies; initialization logs unmet requirements. Hardware AEC never enables P4 software AEC.
- Hardware-AEC conversations default to existing realtime mode (manual hold mode stays manual). Speaking keeps/starts voice processing without resetting input or encoder/send queues.
- AFE reports fresh VAD frames. Detector uses consecutive audio duration AND elapsed time after playback guard; silence, missing frames, playback gaps, reset/disable cancel accumulated speech. Pending callback is once per armed generation and checked again in the main task.
- Main task sends shared Protocol abort, cancels/clears local playback, moves to Listening. Existing realtime upload preserves the beginning of the new question. Existing button/wake-word abort also completes the local state transition for this capability.
- Decoder task carries playback generation, rejects old completion, resets Opus/resampler history on new generation; output task passes generation to codec. Reset/Stop call nonblocking CancelOutput(new_generation), ResumeOutput only precedes current-generation output.
- Receive-side admission mutex orders start/reset, audio packet generation, abort and close. Error, network loss and explicit stop invalidate admission; invalid-state start is rejected before admission and failed main-task transitions revoke it. New start admits first audio before scheduled UI state changes; Speaking no longer resets that newly admitted audio. Same-state start re-arms; old scheduled start/stop/voice callbacks require current epoch/generation. Transport abort/close calls run outside the admission mutex.
- Feature stop waits for queued/in-flight output plus finite stop/last-packet quiet interval (board default 180 ms) via event-only timer. Ordinary boards retain original stop behavior.

## Inspection findings corrected

First implementation review found ResetDecoder cancellation inserted only in Stop; corrected both paths. Further review corrected ordinary-board aborted-stop filtering, same-state response re-arm, initial Auto->Realtime, button abort state completion, and admission start/abort races.

## Checks and limits

Changed-line clang-format and git diff --check passed. Existing host tests are coordinated by parent; this worker did not duplicate them. No generated/vendor output touched.

Physical acceptance still required: clean capture/full duplex, only-playback false triggers, overlapping speech and first-word preservation, short-noise rejection, wake/button interruption, queued tail after stop, delayed MQTT/UDP tail, new reply after abort, disconnect/reconnect, disabled capability and ordinary ES8311 regression. Actual AP AEC depends on correct mode and playback reference wiring. Without server response IDs, a late old TTS start/stop marker or UDP packet arriving after a new start cannot be distinguished perfectly; quiet grace is finite.

## Board contract

VoiceInterruptionConfig {enabled=false,min_speech_ms=200,playback_guard_ms=300,playback_drain_ms=180}; HasHardwareAec()/GetVoiceInterruptionConfig() const default off. CancelOutput(uint32_t generation), ResumeOutput(uint32_t generation), OutputData(vector<int16_t>&,uint32_t generation): cancellation supplies incremented generation, stale queued output must be rejected even after later Resume. Legacy codecs keep default methods.

# Independent final review

Result: static review PASS; existing host suite has one unchanged baseline failure; hardware and compilation NOT RUN by user instruction.

## Scope and evidence

Reviewed the final changes to Application, AudioService, AudioCodec/AudioEngine, AFE VAD callbacks, VoiceInterruption, USB UAC and AP-0316 codecs, board configuration and documentation. No source files or tests were edited by this reviewer. `git diff --check` passed. Existing host suite: `python3 -m unittest discover -s scripts/tests -v`, 62 tests, 61 pass, one unchanged `test_no_broken_quoted_relative_includes` failure at `main/boards/waveshare/esp32-p4-nano/assets/logo__qingdao_tsg.c:5: ../lvgl.h`. Compare `host-baseline.log` and `host-final.log`.

## Reviewed behavior

- Incoming TTS admission is serialized with abort and channel-close using the admission mutex. Reset precedes publication of generation/admission; binary packets admitted before a scheduled speaking transition retain their generation and are no longer cleared by that transition. Protocol sends and close operations run outside this mutex to avoid callbacks reentering it.
- Reset/stop invalidate decoding, disarm pending VAD and cancel hardware output. Dequeued PCM retains its generation; codec checks reject an old frame entering a write after cancellation. New output resumes only its matching generation.
- Confirmed user speech is scheduled onto the main task and checked against output generation and state. Input, AFE and encoding/send queues remain intact during interruption, preserving speech already being captured/uploaded.
- VoiceInterruption requires fresh consecutive VAD speech frames and both audio duration and wall duration. Silence, inactive playback and long fetch gaps reset the confirmation. Guard time begins with output dispatch; a token can trigger only once until rearmed.
- Supported hardware-AEC modes choose realtime and start voice processing when inactive. External AEC does not invent an input reference channel or enable redundant software AEC.
- TTS stop retains admission while output drains and for a bounded quiet interval (180 ms default), resetting the deadline when further packets arrive. Old timer events cannot finish a new response because stop epoch and speaking state are checked.
- AudioCodec defaults leave ordinary codecs on the old path. Hardware AEC disabled, interruption disabled, no audio processor, and unsupported targets do not enable the new interruption path. Existing software/server AEC selection semantics are preserved outside the external-AEC path.
- USB I/O uses separate directional locks/buffers while stream handles remain protected from close; slow control operations run without the global state or opposite direction lock. AP cancellation mutes the PA and later clears TX DMA while leaving the independent microphone input running. The local ESP-IDF 6 headers/source expose the I2S information/preload APIs used by this reset path.

## Fixes raised during review and confirmed in final diff

1. Added missing CancelOutput call to ResetDecoder; without it generation-aware codecs would reject all subsequent frames.
2. Reset disarms prior confirmation and same-state TTS start rearms the new response.
3. Serialized receive admission against main-thread abort/channel-close so stale starts cannot reopen admission after cancellation.
4. Added bounded late-packet drain grace for JSON stop preceding MQTT UDP audio.

## Remaining validation limits

No C/C++ compilation, firmware build, flashing or device tests were performed. Host tests do not execute the audio concurrency paths. Physical tests must cover both AP-0316 transport modes, interruption at reply start/middle/tail, no false trigger during playback alone, preservation of the user's first words, repeated interruptions/new replies, USB disconnect/reconnect, and both WebSocket and MQTT/UDP. USB ringbuffer/hardware and AP analog AEC tuning remain hardware-dependent. Without response IDs in protocol audio/start messages, arbitrary cross-response reordering cannot be strictly isolated; the code uses local generation plus a finite stop quiet interval and must not be described as providing perfect stale-response discrimination.

## Final incremental acceptance

Read-only acceptance of the final additional changes also PASS; `git diff --check` remains clean. No further full host run or compilation was performed for this increment.

- AP preload failure recovery leaves the PA muted, restores a running TX state when possible, and permits the next disable to reset a partial preload cursor. The already-READY retry also performs a real enable/disable cycle. EnableOutput cannot unmute while cancellation or flushing is pending.
- Invalid TTS start states are rejected before admission. A later failed main-task transition withdraws admission and cancels/reset playback. Errors, network disconnect and explicit stop-listening invoke feature-scoped cancellation. Feature-scoped button/wake-word abort moves locally to listening rather than waiting for a deliberately rejected old stop marker.
- New decode generations reset Opus and output resampler history in the codec task before decoding, so an obsolete in-flight decode cannot leave history used by the subsequent response. No additional decoder/resampler ownership conflict was found.

No new definite blocking defect was found in this increment. The physical/protocol limitations above still apply.

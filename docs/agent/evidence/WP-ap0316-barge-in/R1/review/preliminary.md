# AP-0316 voice interruption preliminary review

Scope: read-only analysis and existing Python host tests. No C/C++ compilation, firmware build, flashing, vendor edits, or sdkconfig edits.

Baseline: `python3 -m unittest discover -s scripts/tests -v` ran 62 tests, 61 passed, one existing failure: `test_no_broken_quoted_relative_includes` reports `main/boards/waveshare/esp32-p4-nano/assets/logo__qingdao_tsg.c:5: ../lvgl.h`. Full output: `host-baseline.log`.

## Risks communicated before implementation

1. Incoming `tts/start` is scheduled, but incoming binary audio immediately checks speaking state; start/audio back-to-back can lose first packets. Speaking state handler also calls ResetDecoder, potentially clearing packets admitted before state handling.
2. ResetDecoder invalidates queued/in-flight decoding by generation, but an output task already removed from the queue can call OutputData after reset. Cancellation must cover the dequeue-to-write boundary as well as an active codec write.
3. Realtime speaking only avoids disabling the processor. It must explicitly start processing if it was inactive (idle-to-TTS path). Starting processing currently resets playback and requests 120 ms warmup, so this cannot happen after new response packets have been admitted.
4. AFE VAD callbacks report edges only. Continuous-speech confirmation cannot depend on receiving another true edge. Disabling processing must clear stale VAD state and confirmation state.
5. Interruption must preserve encoder/send queues and keep capture/AFE running to retain the beginning of the user's question. Resetting voice processing or draining send packets at interruption would truncate user speech.
6. Early `tts/stop` can precede decoder/output drain. MQTT audio also arrives on UDP independently of TTS JSON; state-based rejection after stop can truncate late audio. Admission and playback lifetime need explicit boundaries.
7. Neither protocol supplies a response ID in incoming audio. Strict distinction between reordered old/new response start messages is impossible; any policy must state this limit and avoid permanently suppressing future replies if no old stop arrives.
8. USB reads/writes currently share a mutex and PCM buffer. Separate per-direction I/O must preserve handle lifetime against stream close and stop; output cancellation must not shut down capture. AP I2S output cancellation must bound active writes and account for DMA residue.

Final review will inspect actual implementation and run existing host tests only. Python source-string assertions and duplicate model implementations do not validate concurrent firmware behavior.

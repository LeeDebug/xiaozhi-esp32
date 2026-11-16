# Independent frame-gap integration review

Reviewed read-only: `web/framing.mjs`, `web/framing.test.mjs`, `app.mjs` connection
and receive paths, and the existing SerialSession receive/close implementation.
No implementation files changed during this review.

Result: PASS within browser-observed receive timing; no blocking issue found.

- Default gap derives from 3.5 ten-bit 8N1 characters, rounded upward to integer
  milliseconds: 9600 baud uses 4 ms; high baud uses the rounded 1.75 ms rule.
- Every non-empty chunk extends the deadline; empty reads cannot prolong or
  prematurely close an interval. The decoder receives chunks unchanged.
- Timer callbacks check actual monotonic elapsed time, rearming when early.
  They do not flush until the configured observed silence period has elapsed.
- When a new chunk arrives after an elapsed gap and before a late timer runs,
  the old interval is flushed before appending new bytes. The canceled timer's
  generation cannot later flush the new interval.
- Reset cancels pending timers, increments the generation, clears last-receive
  time and resets the decoder. Canceled callbacks that have already been queued
  are harmless. Gap changes reset pending data and are locked during connection.
- App establishes the configured gap before opening, routes serial bytes through
  GapFramer, and resets framing on explicit disconnect and connection close.
  All live decoder dispatch therefore occurs after an observed gap, and the
  decoder validates/rejects the complete interval without payload scanning.
- Offline whole-frame injection intentionally bypasses receive timing and uses
  ModbusSlave's whole-frame checks; it does not search for embedded subframes.

Independently reran protocol, framing, and serial tests together: 35 PASS, 0 FAIL.
Framing tests specifically exercise early/late callbacks, stale callbacks, exact
deadline boundaries, fragmentation, same-interval merged requests, reset and
changed-gap behavior, with actual RtuDecoder and ModbusSlave integration.

Limitations: JavaScript sees delivery times of chunks, not individual UART byte
timestamps. USB/driver coalescing can hide a wire gap; scheduling delay can make
delivery timing differ from wire timing. These tests prove the policy for observed
timing and do not establish precise physical RTU compliance or foreground/background
browser response latency. Real adapter/board timing remains physical validation.
No hardware, firmware compilation, flashing, or additional agents were used.

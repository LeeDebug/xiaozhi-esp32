# Strict interval-based Modbus decoding

Scope: only `web/protocol.mjs` RtuDecoder and framing tests in
`web/protocol.test.mjs`. ModbusSlave and register semantics are unchanged.

API is unchanged: `push(bytes)` accumulates an interval, `flush()` confirms its
ending frame gap, `reset()` clears it. No callback runs from `push()`. `flush()`
validates only the complete interval: address 0..247, 4..256 bytes, whole-frame
CRC, and exactly eight bytes for function 03 or 06. Rejected intervals are never
searched for a valid prefix, suffix, or embedded request.

Intervals exceeding 256 bytes retain only their first 256 diagnostic bytes and
remain invalid until flush/reset; a retained CRC-valid prefix or subsequent
CRC-valid write cannot execute. Decoder state resets before frame/discard
callbacks, including for reentrant callbacks. Empty flush is a no-op.

PASS: all 18 protocol tests; see `protocol-tests.log`.
PASS: Node syntax checks on both modules.
PASS: scoped `git diff --check`.

Framing regressions cover every split of an eight-byte request, no immediate
emission after complete push, single emission after flush, requests without a
gap rejected together, noise and corrupt/truncated intervals containing valid
write commands never executing them, variable-length unknown function containing
a valid write handled whole, valid-CRC malformed 03/06 lengths, invalid address,
overflow with valid prefix/tail and recovery next interval, reset, callback
reentrancy and empty flush. Existing CRC/read/write/exception/broadcast tests pass.

Limit: this byte-only decoder relies on its caller to determine frame gaps.
Browser read chunks do not expose precise per-byte UART timestamps; a caller
cannot reconstruct a physical RTU gap hidden inside an already coalesced chunk.
No firmware compilation, flashing, physical serial access, or hardware timing
validation was performed. No additional agents or persistent processes acquired.

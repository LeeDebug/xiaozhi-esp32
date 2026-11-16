# Protocol engine verification

Scope: `web/protocol.mjs` and `web/protocol.test.mjs` only.

PASS: Node protocol regression, 13 tests; see `tests.log`.
PASS: `node --check` on both modules.

Independent CRC check vectors: ASCII `123456789` gives CRC-16/MODBUS `0x4b37`;
request `01 03 00 00 00 0A` appends `C5 CD`.

Coverage: read/write encoding, 78-register reads, final register, generic register
105 access, exceptions 01/02/03, malformed/CRC-invalid and foreign-address silence,
broadcast writes without replies, low-word-first signed silo representation,
every split point of an eight-byte request, concatenated frames, noise/corrupt CRC
and truncated-prefix recovery, idle-boundary unknown-function extraction, bounded
256-byte retention and discard callbacks under 10,000 bytes of noise, reset.

NOT_RUN: serial hardware, physical RTU timing, firmware compilation/flashing.
Browser RTU framing uses length plus CRC for firmware functions 03 and 06; the
caller must call `flush()` at its idle boundary for unsupported functions. Browser
USB scheduling cannot provide precise wire-level t3.5 measurement. A CRC-valid
pattern inside arbitrary noise may be accepted, as no framing marker exists in RTU.

No hardware resources, servers or child agents were acquired.

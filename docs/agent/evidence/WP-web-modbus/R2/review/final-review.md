# Parent acceptance — 2026-11-04

PASS: all 35 Node tests (18 protocol, 8 timing, 9 serial); Node syntax checks for
app, framing and protocol; git diff --check. Independent gap review also PASS.

Critical behavioral evidence: an 8-byte write cannot execute before the idle
interval; noise/concatenated frames and overflow cannot expose embedded writes;
multiple USB chunks within an interval are validated once as a whole; late timers
cannot merge two observed intervals, early timers cannot flush prematurely, stale
callbacks/reset/reconfigure cannot replay pending data. Overflow remains latched
until the boundary, with at most 256 bytes retained. Decoder callback state resets
before emission. The UI routes serial data exclusively via GapFramer, clears
pending data at disconnect/error, ignores late read delivery on inactive sessions.

Browser PASS: new gap input default4,19200=>2,9600=>4,manual numeric edit=>5;
existing offline water read returns25500. Reload and module loading succeeds,
no console warnings/errors. preview.png shows default4 after reload.

NOT_RUN: hardware UART/RS485/USB timing and actual reconnect, firmware build/flash.
Web Serial timestamps describe chunks, not wire idle; merged driver chunks cannot
be separated and are deliberately discarded. Hidden per-byte t1.5 errors cannot
be detected. Timing limitations and setup guidance are documented in web README.
User's pre-existing web.zip was not changed. No deployments or hardware operations.

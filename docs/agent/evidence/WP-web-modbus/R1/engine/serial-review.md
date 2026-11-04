# Serial and app directed review

PASS: 9 Node tests against the actual `serial.mjs`, using native Web Streams fake
ports; see `serial-tests.log`. No physical serial access or firmware build.

Coverage: firmware 9600-8N1 opening options; raw byte reception/transmission;
ordered writes and lock release; disconnect cancels a 3-second delay promptly
and suppresses queued responses; 64-response queue bound; read rejection and
EOF cleanup; write rejection lock release and queue recovery; open failure retry;
concurrent close and session reopen.

Write rejection limitation: a Web WritableStream becomes errored after rejecting
a write. Recovery test replaces the errored underlying stream to establish that
SerialSession's promise chain is recoverable. It does not assert a physically
failed UART can recover without reconnection.

App review findings sent to parent for implementation:

1. `processFrame()` synchronously validates the live delay input. A user can have
   a temporarily empty/invalid value while editing. A received RTU request then
   throws from the read callback and closes the serial session. Use a committed,
   previously validated delay setting or a safe fallback at response dispatch.
2. The serial-error notice is overwritten by the close handler's generic
   disconnected message, obscuring the reason for unexpected disconnection.

Remaining verification limits: native browser permission selection, USB adapter
drivers, hardware half duplex direction, signal wiring and wire RTU timing.

No serial or firmware implementation files were modified by the reviewer.

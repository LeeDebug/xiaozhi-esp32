import test from 'node:test';
import assert from 'node:assert/strict';
import { SerialSession } from './serial.mjs';

const tick = () => new Promise(resolve => setImmediate(resolve));
class FakePort {
  constructor(write = () => {}) {
    this.writes = [];
    this.closes = 0;
    this.readable = new ReadableStream({ start: controller => { this.input = controller; } });
    this.writable = new WritableStream({ write: async bytes => { await write(bytes); this.writes.push(Uint8Array.from(bytes)); } });
  }
  async open(options) { this.options = options; }
  async close() {
    assert.equal(this.readable.locked, false, 'reader lock released before port.close');
    assert.equal(this.writable.locked, false, 'writer lock released before port.close');
    this.closes++;
  }
}
function createSession() {
  const received = [], errors = [], state = { closed: 0 };
  const session = new SerialSession(bytes => received.push(bytes), error => errors.push(error), () => state.closed++);
  return { session, received, errors, state };
}

test('opens with firmware 9600-8N1 settings; receives raw bytes and writes without encoding', async () => {
  const port = new FakePort(); const { session, received, state, errors } = createSession();
  await session.open(port);
  assert.deepEqual(port.options, { baudRate: 9600, dataBits: 8, stopBits: 1, parity: 'none', flowControl: 'none' });
  const incoming = Uint8Array.of(1, 3, 0, 108, 0, 1, 0, 0);
  port.input.enqueue(incoming); await tick();
  assert.deepEqual(received, [incoming]);
  let sent = false;
  assert.equal(await session.send(Uint8Array.of(1, 3, 2, 97, 168), 0, () => { sent = true; }), true);
  assert.deepEqual(port.writes, [Uint8Array.of(1, 3, 2, 97, 168)]);
  assert.equal(sent, true);
  await session.close();
  assert.equal(session.active, false); assert.equal(session.port, null);
  assert.equal(port.closes, 1); assert.equal(state.closed, 1); assert.deepEqual(errors, []);
});

test('ordered queue waits for each actual write; writer lock is released between frames', async () => {
  let release;
  const held = new Promise(resolve => { release = resolve; });
  const port = new FakePort(async bytes => { if (bytes[0] === 1) await held; });
  const { session } = createSession(); await session.open(port);
  const first = session.send(Uint8Array.of(1)); const second = session.send(Uint8Array.of(2));
  await tick(); assert.deepEqual(port.writes, []);
  release();
  assert.deepEqual(await Promise.all([first, second]), [true, true]);
  assert.deepEqual(port.writes.map(bytes => bytes[0]), [1, 2]);
  assert.equal(port.writable.locked, false);
  await session.close(); assert.equal(session.pending, 0);
});

test('disconnect cancels delayed responses and queued responses without writing', async () => {
  const port = new FakePort(); const { session } = createSession(); await session.open(port);
  let sent = 0;
  const first = session.send(Uint8Array.of(1), 3000, () => sent++);
  const second = session.send(Uint8Array.of(2), 0, () => sent++);
  await tick();
  await session.close();
  assert.deepEqual(await Promise.all([first, second]), [false, false]);
  assert.deepEqual(port.writes, []); assert.equal(sent, 0); assert.equal(session.pending, 0);
  assert.equal(await session.send(Uint8Array.of(3)), false);
});

test('queue is bounded and remains clean after overflow and close', async () => {
  const port = new FakePort(); const { session } = createSession(); await session.open(port);
  const tasks = Array.from({ length: 64 }, () => session.send(Uint8Array.of(1), 3000));
  await assert.rejects(session.send(Uint8Array.of(2)), /队列已满/);
  await session.close();
  assert.ok((await Promise.all(tasks)).every(result => result === false));
  assert.equal(session.pending, 0); assert.deepEqual(port.writes, []);
});

test('read failure automatically closes and releases reader with queued response canceled', async () => {
  const port = new FakePort(); const { session, errors, state } = createSession(); await session.open(port);
  const pending = session.send(Uint8Array.of(1), 3000);
  const unplug = new Error('device removed'); port.input.error(unplug);
  await tick(); if (session.closing) await session.closing;
  assert.equal(await pending, false);
  assert.deepEqual(errors, [unplug]); assert.equal(state.closed, 1); assert.equal(port.closes, 1);
  assert.equal(session.port, null); assert.equal(port.readable.locked, false);
});

test('read EOF cleans up as a disconnect', async () => {
  const port = new FakePort(); const { session, state, errors } = createSession(); await session.open(port);
  port.input.close(); await tick(); if (session.closing) await session.closing;
  assert.equal(state.closed, 1); assert.deepEqual(errors, []); assert.equal(session.port, null);
});

test('write rejection releases its lock and does not poison subsequent queue tasks', async () => {
  const failed = new Error('write failed');
  const port = new FakePort(() => { throw failed; }); const { session } = createSession(); await session.open(port);
  await assert.rejects(session.send(Uint8Array.of(1)), error => error === failed);
  assert.equal(port.writable.locked, false);
  // A Web WritableStream remains errored after rejection. A recovered/replaced
  // underlying stream lets us check the SerialSession promise queue separately.
  port.writable = new WritableStream({ write: bytes => port.writes.push(Uint8Array.from(bytes)) });
  assert.equal(await session.send(Uint8Array.of(2)), true);
  assert.deepEqual(port.writes, [Uint8Array.of(2)]);
  await session.close(); assert.equal(session.pending, 0);
});

test('port open failure does not acquire readers or prevent retry', async () => {
  const broken = new FakePort(); broken.open = async () => { throw new Error('permission denied'); };
  const { session } = createSession();
  await assert.rejects(session.open(broken), /permission denied/);
  assert.equal(session.port, null); assert.equal(session.active, false); assert.equal(broken.readable.locked, false);
  const next = new FakePort(); await session.open(next); await session.close(); assert.equal(next.closes, 1);
});

test('concurrent close calls share cleanup and session can reopen', async () => {
  const { session, state } = createSession(); const port = new FakePort(); await session.open(port);
  const a = session.close(), b = session.close(); assert.equal(a, b); await a;
  assert.equal(state.closed, 1); assert.equal(port.closes, 1);
  const next = new FakePort(); await session.open(next); await session.close();
  assert.equal(next.closes, 1); assert.equal(state.closed, 2);
});

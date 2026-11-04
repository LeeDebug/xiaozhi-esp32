import test from 'node:test';
import assert from 'node:assert/strict';
import { GapFramer, defaultFrameGap } from './framing.mjs';
import { RtuDecoder, ModbusSlave, withCrc } from './protocol.mjs';

const read = withCrc([1, 3, 0, 108, 0, 1]);
const write = withCrc([1, 6, 0, 59, 0, 75]);
function harness(gapMs = 4) {
  let time = 0, id = 0;
  const tasks = new Map(), all = new Map(), frames = [], drops = [];
  const slave = new ModbusSlave();
  const decoder = new RtuDecoder(frame => { frames.push(frame); slave.handle(frame); }, (bytes, reason) => drops.push({ bytes, reason }));
  const framer = new GapFramer(decoder, { gapMs, now: () => time,
    schedule: (fn, ms) => { const task = { fn, due: time + ms }; tasks.set(++id, task); all.set(id, task); return id; },
    cancel: id => tasks.delete(id) });
  return { framer, frames, drops, slave, tasks, all,
    at: value => { time = value; },
    tick: value => {
      time = value;
      // Deliberately execute callbacks at the observed clock, as a busy browser does.
      for (const [id, task] of [...tasks]) if (task.due <= time && tasks.delete(id)) task.fn();
    } };
}

test('frame gap follows the firmware 8N1 baud rate and accepts only bounded integers', () => {
  assert.equal(defaultFrameGap(9600), 4);
  assert.equal(defaultFrameGap(19200), 2);
  assert.equal(defaultFrameGap(115200), 2);
  assert.throws(() => defaultFrameGap(0));
  const h = harness();
  for (const n of [0, -1, 1001, NaN, 2.5]) assert.throws(() => h.framer.setGap(n));
});

test('even a complete write cannot execute before the receive idle interval ends', () => {
  const h = harness();
  h.framer.push(write);
  h.tick(3.99);
  assert.equal(h.frames.length, 0);
  assert.equal(h.slave.registers[59], 0);
  h.tick(4);
  assert.deepEqual(h.frames, [write]);
  assert.equal(h.slave.registers[59], 75);
  h.tick(100);
  assert.equal(h.frames.length, 1);
});

test('fragmented chunks extend the deadline from the last non-empty receive', () => {
  const h = harness();
  h.framer.push(write.slice(0, 3));
  h.at(3); h.framer.push(write.slice(3));
  h.at(5); h.framer.push(new Uint8Array());
  h.tick(6.99); assert.equal(h.frames.length, 0);
  h.tick(7); assert.deepEqual(h.frames, [write]);
});

test('a late timer cannot merge requests separated by an observed frame gap', () => {
  const h = harness();
  h.framer.push(read);
  const stale = [...h.all.values()][0].fn;
  h.at(4); h.framer.push(write); // Timer has not run, but the boundary has elapsed.
  assert.deepEqual(h.frames, [read]);
  stale(); // A canceled callback that was already queued must be harmless.
  assert.equal(h.frames.length, 1);
  h.tick(8);
  assert.deepEqual(h.frames, [read, write]);
});

test('early timer wakeups must wait out the entire idle interval', () => {
  const h = harness();
  h.framer.push(write);
  h.at(2); [...h.tasks.values()][0].fn();
  assert.equal(h.frames.length, 0);
  h.tick(4); assert.deepEqual(h.frames, [write]);
});

test('two requests within the same idle interval are rejected without executing a write', () => {
  const h = harness();
  h.framer.push(read);
  h.at(3); h.framer.push(write);
  h.tick(7);
  assert.equal(h.frames.length, 0);
  assert.equal(h.slave.registers[59], 0);
  assert.equal(h.drops.length, 1);
  assert.equal(h.drops[0].bytes.length, 16);
});

test('reset on disconnect cancels timers and discards partial bytes across reconnect', () => {
  const h = harness();
  h.framer.push(write.slice(0, 4));
  const stale = [...h.tasks.values()][0].fn;
  h.framer.reset();
  h.at(4); stale();
  assert.equal(h.frames.length, 0); assert.equal(h.drops.length, 0);
  h.framer.push(read); h.tick(8);
  assert.deepEqual(h.frames, [read]);
});

test('changing the configured interval cancels the old deadline and pending frame', () => {
  const h = harness();
  h.framer.push(write);
  h.framer.setGap(20);
  h.at(1); h.framer.push(read);
  h.tick(5); assert.equal(h.frames.length, 0);
  h.tick(21); assert.deepEqual(h.frames, [read]);
  assert.equal(h.slave.registers[59], 0);
});

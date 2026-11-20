import test from 'node:test';
import assert from 'node:assert/strict';
import { crc16, withCrc, hex, parseHex, RtuDecoder, ModbusSlave } from './protocol.mjs';

const request = (fn, start, value, address = 1) => withCrc([address, fn, start >>> 8, start & 255, value >>> 8, value & 255]);

test('CRC agrees with published Modbus example and ASCII check vector', () => {
  assert.equal(crc16(new TextEncoder().encode('123456789')), 0x4b37);
  assert.equal(hex(withCrc([1, 3, 0, 0, 0, 10])), '01 03 00 00 00 0A C5 CD');
  assert.equal(crc16(parseHex('01 03 00 00 00 0A C5 CD')), 0);
});

test('hex parser rejects partial, invalid and empty input', () => {
  assert.deepEqual(parseHex(' 01\n03 00\t6C '), Uint8Array.of(1, 3, 0, 108));
  for (const invalid of ['', ' ', '1', '01 0G', '0x01', '01,03']) {
    assert.throws(() => parseHex(invalid));
  }
});

test('03 responds with big-endian register bytes, including all 78 firmware registers', () => {
  const slave = new ModbusSlave();
  slave.registers[0x6c] = 25000;
  assert.equal(hex(slave.handle(request(3, 0x6c, 1)).response), hex(withCrc([1, 3, 2, 0x61, 0xa8])));
  const all = slave.handle(request(3, 0, 78)).response;
  assert.equal(all.length, 161);
  assert.equal(all[2], 156);
  assert.equal(crc16(all), 0);
  assert.equal(slave.handle(request(3, 166, 1)).response.length, 7);
});

test('06 echoes request, mutates one register, and subsequent read observes it', () => {
  const slave = new ModbusSlave();
  const write = request(6, 0x6b, 1);
  const result = slave.handle(write);
  assert.deepEqual(result.response, write);
  assert.notEqual(result.response, write);
  assert.deepEqual(result.changed, [107]);
  assert.equal(slave.registers[107], 1);
  assert.equal(slave.handle(request(3, 107, 1)).response[4], 1);
  slave.handle(request(6, 105, 65535));
  assert.equal(slave.registers[105], 65535);
});

test('invalid function, range, and quantity return appropriate exception frames', () => {
  const slave = new ModbusSlave();
  for (const [frame, code] of [[request(4, 0, 1), 1], [request(3, 0, 0), 3],
    [request(3, 0, 126), 3], [request(3, 166, 2), 2], [request(6, 167, 1), 2]]) {
    const result = slave.handle(frame);
    assert.deepEqual(result.response, withCrc([1, frame[1] | 0x80, code]));
    assert.deepEqual(result.changed, []);
  }
});

test('CRC, malformed requests, and foreign addresses are silent and never write', () => {
  const slave = new ModbusSlave();
  const bad = request(6, 59, 100); bad[7] ^= 1;
  for (const frame of [bad, request(6, 59, 100, 2), withCrc([1, 6, 0, 59, 0]), new Uint8Array()]) {
    assert.equal(slave.handle(frame).response, null);
    assert.equal(slave.registers[59], 0);
  }
  slave.address = 2;
  assert.equal(slave.handle(request(6, 59, 100)).response, null);
  assert.ok(slave.handle(request(6, 59, 100, 2)).response);
});

test('broadcast writes update without replying; reads and invalid writes remain silent', () => {
  const slave = new ModbusSlave();
  const result = slave.handle(request(6, 60, 75, 0));
  assert.equal(result.response, null);
  assert.deepEqual(result.changed, [60]);
  assert.equal(slave.registers[60], 75);
  assert.equal(slave.handle(request(3, 60, 1, 0)).response, null);
  assert.equal(slave.handle(request(6, 167, 1, 0)).response, null);
  assert.equal(slave.handle(request(4, 0, 1, 0)).response, null);
});

test('silo low-word-first int32 encoding is returned unchanged on wire', () => {
  const slave = new ModbusSlave();
  // -70000 = 0xfffeee90: firmware reads low word first, but each word is big endian.
  slave.registers[2] = 0xee90; slave.registers[3] = 0xfffe;
  assert.deepEqual(slave.handle(request(3, 2, 2)).response, withCrc([1, 3, 4, 0xee, 0x90, 0xff, 0xfe]));
});

test('decoder emits exactly once at a confirmed gap for every USB split position', () => {
  const a = request(3, 108, 1);
  for (let split = 0; split <= 8; split++) {
    const frames = [];
    const decoder = new RtuDecoder(frame => frames.push(frame));
    decoder.push(a.slice(0, split));
    assert.deepEqual(frames, []);
    decoder.push(a.slice(split));
    assert.deepEqual(frames, [], 'even all eight bytes must wait for a gap');
    decoder.flush();
    assert.deepEqual(frames, [a]);
    assert.equal(decoder.buffer.length, 0);
    decoder.flush();
    assert.deepEqual(frames, [a], 'empty flush does not replay a frame');
  }
});

test('requests without an intervening gap are rejected as one interval and never execute writes', () => {
  const slave = new ModbusSlave(), frames = [], discarded = [];
  const decoder = new RtuDecoder(frame => { frames.push(frame); slave.handle(frame); }, bytes => discarded.push(bytes));
  const read = request(3, 108, 1), write = request(6, 59, 100);
  decoder.push(read); decoder.push(write); decoder.push(write);
  assert.deepEqual(frames, []); assert.deepEqual(discarded, []);
  decoder.flush();
  assert.deepEqual(frames, []);
  assert.deepEqual(discarded, [Uint8Array.from([...read, ...write, ...write])]);
  assert.equal(slave.registers[59], 0);
  decoder.push(write); decoder.flush();
  assert.equal(frames.length, 1); assert.equal(slave.registers[59], 100);
});

test('valid-looking writes inside noisy or CRC-invalid intervals are never extracted', () => {
  const write = request(6, 60, 35), bad = Uint8Array.from(write); bad[6] ^= 0x80;
  for (const interval of [
    [255, ...write], [...write, 255], [255, ...write, 255],
    [...write.slice(0, 4), ...write], [...bad, ...write], [...bad], [...write.slice(0, 5)],
  ]) {
    const slave = new ModbusSlave(), frames = [], discarded = [];
    const decoder = new RtuDecoder(frame => { frames.push(frame); slave.handle(frame); }, bytes => discarded.push(bytes));
    decoder.push(Uint8Array.from(interval)); decoder.flush();
    assert.deepEqual(frames, [], `must reject entire interval ${hex(interval)}`);
    assert.deepEqual(discarded, [Uint8Array.from(interval)]);
    assert.equal(slave.registers[60], 0);
    decoder.push(write); decoder.flush();
    assert.equal(slave.registers[60], 35, 'next interval recovers independently');
  }
});

test('complete unsupported function is emitted at idle and receives illegal-function exception', () => {
  const frames = [];
  const decoder = new RtuDecoder(frame => frames.push(frame));
  const unknown = request(4, 0, 2);
  decoder.push(unknown);
  assert.equal(frames.length, 0);
  decoder.flush();
  assert.deepEqual(frames, [unknown]);
  assert.deepEqual(new ModbusSlave().handle(frames[0]).response, withCrc([1, 0x84, 1]));
});

test('variable-length unknown function is handled whole even with an embedded valid write', () => {
  const slave = new ModbusSlave(), frames = [], discarded = [];
  const decoder = new RtuDecoder(frame => { frames.push(frame); slave.handle(frame); }, bytes => discarded.push(bytes));
  const embedded = request(6, 59, 100);
  const unsupported = withCrc([1, 0x10, 0, 1, 0, 4, 8, ...embedded]);
  decoder.push(unsupported);
  assert.deepEqual(frames, []);
  decoder.flush();
  assert.deepEqual(frames, [unsupported]); assert.deepEqual(discarded, []);
  assert.equal(slave.registers[59], 0);
  assert.deepEqual(slave.handle(frames[0]).response, withCrc([1, 0x90, 1]));
});

test('03/06 with valid CRC but incorrect length are discarded as entire intervals', () => {
  const frames = [], discarded = [];
  const decoder = new RtuDecoder(frame => frames.push(frame), bytes => discarded.push(bytes));
  for (const fn of [3, 6]) {
    for (const payload of [[1, fn, 0], [1, fn, 0, 59, 0, 1, 0]]) {
      const frame = withCrc(payload);
      assert.equal(crc16(frame), 0);
      decoder.push(frame); decoder.flush();
      assert.deepEqual(discarded.at(-1), frame);
    }
  }
  assert.deepEqual(frames, []); assert.equal(discarded.length, 4);
});

test('overflow invalidates the whole interval including a CRC-valid retained prefix and valid tail', () => {
  for (const noiseLength of [257, 10000]) {
    const slave = new ModbusSlave(), frames = [], discarded = [];
    const decoder = new RtuDecoder(frame => { frames.push(frame); slave.handle(frame); },
      (bytes, reason) => discarded.push({ bytes, reason }));
    const write = request(6, 59, 35);
    // A 256-byte, CRC-valid unknown-function prefix must not be accepted after overflow.
    const prefix = withCrc([1, 0x10, ...new Uint8Array(252)]);
    decoder.push(prefix); decoder.push(new Uint8Array(noiseLength).fill(255)); decoder.push(write);
    assert.equal(decoder.buffer.length, 256); assert.deepEqual(discarded, []); assert.deepEqual(frames, []);
    decoder.flush();
    assert.deepEqual(frames, []); assert.equal(slave.registers[59], 0);
    assert.equal(discarded.length, 1); assert.equal(discarded[0].bytes.length, 256);
    assert.deepEqual(discarded[0].bytes, prefix); assert.match(discarded[0].reason, /超过 256/);
    assert.equal(decoder.buffer.length, 0); assert.equal(decoder.overflow, false);
    decoder.push(write); decoder.flush();
    assert.equal(frames.length, 1); assert.equal(slave.registers[59], 35);
  }
});

test('reset clears pending and overflow state without a callback', () => {
  const frames = [], discarded = [];
  const decoder = new RtuDecoder(frame => frames.push(frame), bytes => discarded.push(bytes));
  const frame = request(6, 59, 35);
  decoder.push(new Uint8Array(1000)); decoder.reset();
  assert.equal(decoder.buffer.length, 0);
  assert.equal(decoder.overflow, false);
  decoder.push(frame.slice(0, 4)); decoder.reset(); decoder.push(frame.slice(4)); decoder.flush();
  assert.deepEqual(frames, []); assert.equal(discarded.length, 1);
  decoder.push(frame); decoder.flush();
  assert.deepEqual(frames, [frame]);
});

test('invalid RTU address is rejected despite a correct whole-frame CRC', () => {
  const frames = [], discarded = [];
  const decoder = new RtuDecoder(frame => frames.push(frame), bytes => discarded.push(bytes));
  const frame = request(6, 59, 35, 248);
  decoder.push(frame); decoder.flush();
  assert.deepEqual(frames, []); assert.deepEqual(discarded, [frame]);
});

test('frame and discard callbacks observe reset state and may enqueue the next interval', () => {
  const first = request(3, 108, 1), second = request(6, 59, 35);
  const frames = [], discarded = [];
  const decoder = new RtuDecoder(frame => {
    assert.equal(decoder.buffer.length, 0); assert.equal(decoder.overflow, false);
    frames.push(frame);
    decoder.flush();
    if (frames.length === 1) decoder.push(second);
  }, bytes => {
    assert.equal(decoder.buffer.length, 0); assert.equal(decoder.overflow, false);
    discarded.push(bytes); decoder.flush(); decoder.push(first);
  });
  decoder.push(Uint8Array.of(255)); decoder.flush();
  assert.equal(discarded.length, 1); assert.deepEqual(frames, []);
  decoder.flush(); assert.deepEqual(frames, [first]);
  decoder.flush(); assert.deepEqual(frames, [first, second]);
});

// Web Serial reports chunks, not wire-level byte timestamps. These boundaries
// are based on observed receive silence; never infer boundaries from payloads.
export function defaultFrameGap(baudRate) {
  if (!Number.isFinite(baudRate) || baudRate <= 0) throw new RangeError('波特率无效');
  // Firmware uses 8N1 (10 bits/character). Round up for millisecond timers.
  return Math.ceil(baudRate > 19200 ? 1.75 : 35000 / baudRate);
}

export class GapFramer {
  constructor(decoder, { gapMs = 4, now = () => performance.now(),
    schedule = (fn, ms) => setTimeout(fn, ms), cancel = id => clearTimeout(id) } = {}) {
    Object.assign(this, { decoder, now, schedule, cancel, timer: null, lastReceive: null, generation: 0 });
    this.setGap(gapMs);
  }

  setGap(gapMs) {
    if (!Number.isInteger(gapMs) || gapMs < 1 || gapMs > 1000) throw new RangeError('帧间隔需为 1–1000 ms 整数');
    this.reset();
    this.gapMs = gapMs;
  }

  reset() {
    ++this.generation;
    if (this.timer !== null) this.cancel(this.timer);
    this.timer = null;
    this.lastReceive = null;
    this.decoder.reset();
  }

  push(bytes) {
    if (!bytes.length) return;
    const receivedAt = this.now();
    if (this.timer !== null) this.cancel(this.timer);
    // If the timer was delayed by the event loop, preserve an already observed
    // boundary before appending a new chunk instead of merging two intervals.
    if (this.lastReceive !== null && receivedAt - this.lastReceive >= this.gapMs) {
      this.lastReceive = null;
      this.decoder.flush();
    }
    this.lastReceive = receivedAt;
    this.decoder.push(bytes);
    this.arm(++this.generation, this.gapMs);
  }

  arm(generation, waitMs) {
    this.timer = this.schedule(() => {
      if (generation !== this.generation || this.lastReceive === null) return;
      this.timer = null;
      const remaining = this.gapMs - (this.now() - this.lastReceive);
      // Browser timers may fire early. Only flush after the full idle period.
      if (remaining > 0) { this.arm(generation, remaining); return; }
      this.lastReceive = null;
      this.decoder.flush();
    }, waitMs);
  }
}

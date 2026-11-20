// One reader and a bounded, ordered response queue per connection.
export class SerialSession {
  constructor(onBytes, onError, onClose) {
    Object.assign(this, { onBytes, onError, onClose, port: null, reader: null,
      active: false, pending: 0, tail: Promise.resolve(), closing: null });
  }
  async open(port, baudRate = 9600) {
    if (this.port || this.closing) throw new Error('串口正在使用');
    await port.open({ baudRate, dataBits: 8, stopBits: 1, parity: 'none', flowControl: 'none' });
    this.port = port;
    this.active = true;
    this.readTask = this.readLoop();
  }
  async readLoop() {
    try {
      while (this.active && this.port?.readable) {
        this.reader = this.port.readable.getReader();
        try {
          while (this.active) {
            const { value, done } = await this.reader.read();
            if (done) return;
            if (value?.length) this.onBytes(value);
          }
        } finally {
          this.reader.releaseLock();
          this.reader = null;
        }
      }
    } catch (error) {
      if (this.active) this.onError(error);
    } finally {
      if (this.active) void this.close();
    }
  }
  send(bytes, delay = 0, onSent = () => {}) {
    if (!this.active) return Promise.resolve(false);
    if (this.pending >= 64) return Promise.reject(new Error('响应队列已满，已丢弃请求'));
    ++this.pending;
    const task = this.tail.then(async () => {
      if (!this.active) return false;
      if (delay) await new Promise(resolve => {
        this.cancelDelay = resolve;
        this.delayTimer = setTimeout(resolve, delay);
      });
      this.cancelDelay = null;
      clearTimeout(this.delayTimer);
      if (!this.active || !this.port?.writable) return false;
      const writer = this.port.writable.getWriter();
      try { await writer.write(bytes); } finally { writer.releaseLock(); }
      onSent();
      return true;
    });
    this.tail = task.catch(() => {}).finally(() => { --this.pending; });
    return task;
  }
  close() {
    if (this.closing) return this.closing;
    this.active = false;
    clearTimeout(this.delayTimer);
    this.cancelDelay?.();
    this.closing = (async () => {
      try {
        if (this.reader) await this.reader.cancel().catch(() => {});
        await this.readTask;
        await this.tail;
        if (this.port) await this.port.close();
      } catch (error) { this.onError(error); }
      finally {
        this.port = null;
        this.closing = null;
        this.onClose();
      }
    })();
    return this.closing;
  }
}

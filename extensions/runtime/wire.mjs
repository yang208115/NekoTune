import { EventEmitter } from 'node:events';

// Separate sockets keep console output and third-party package logging out of RPC framing.
export class Wire extends EventEmitter {
  constructor(socket, timeout = 30_000) {
    super();
    this.socket = socket;
    this.timeout = timeout;
    this.nextId = 1;
    this.pending = new Map();
    this.buffer = '';
    socket.setEncoding('utf8');
    socket.on('data', (chunk) => {
      this.buffer += chunk;
      if (Buffer.byteLength(this.buffer) > 8 * 1024 * 1024)
        return socket.destroy(new Error('RPC frame too large'));
      let newline;
      while ((newline = this.buffer.indexOf('\n')) >= 0) {
        const line = this.buffer.slice(0, newline);
        this.buffer = this.buffer.slice(newline + 1);
        if (!line) continue;
        let message;
        try {
          message = JSON.parse(line);
        } catch {
          socket.destroy(new Error('Invalid RPC JSON'));
          return;
        }
        if (message.status) {
          if (!this.pending.has(message.id)) continue;
          const pending = this.pending.get(message.id);
          this.pending.delete(message.id);
          clearTimeout(pending.timer);
          message.status === 'ok'
            ? pending.resolve(message.data ?? {})
            : pending.reject(new Error(message.message || 'Request failed'));
        } else this.emit('message', message);
      }
    });
    socket.on('error', (error) => this.emit('fault', error));
    socket.on('close', () => {
      for (const pending of this.pending.values()) {
        clearTimeout(pending.timer);
        pending.reject(new Error('Extension connection closed'));
      }
      this.pending.clear();
      this.emit('close');
    });
  }
  send(message) {
    if (this.socket.destroyed) return false;
    if (this.socket.writableLength > 8 * 1024 * 1024) {
      this.socket.destroy(new Error('RPC backpressure limit'));
      return false;
    }
    return this.socket.write(JSON.stringify(message) + '\n');
  }
  request(method, params = {}, timeout = this.timeout) {
    return new Promise((resolve, reject) => {
      if (this.socket.destroyed) return reject(new Error('Extension connection closed'));
      const id = this.nextId++;
      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(new Error(`Request timed out: ${method}`));
      }, timeout);
      this.pending.set(id, { resolve, reject, timer });
      try {
        this.send({ id, method, params });
      } catch (error) {
        clearTimeout(timer);
        this.pending.delete(id);
        reject(error);
      }
    });
  }
  async handle(message, handler) {
    try {
      const data = await handler(message.method, message.params ?? {});
      if (message.id !== undefined) this.send({ id: message.id, status: 'ok', data: data ?? {} });
    } catch (error) {
      if (message.id !== undefined)
        this.send({ id: message.id, status: 'error', message: String(error.message || error) });
    }
  }
}

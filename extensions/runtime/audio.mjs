import http from 'node:http';
import https from 'node:https';
import fs from 'node:fs/promises';
import { createWriteStream } from 'node:fs';
import path from 'node:path';
import { randomBytes, randomUUID } from 'node:crypto';
import { pipeline } from 'node:stream/promises';
import { Transform } from 'node:stream';

function validate(audio) {
  const url = new URL(audio.url);
  if (!['http:', 'https:'].includes(url.protocol))
    throw new Error('Audio resolver must return an HTTP(S) URL');
  const headers = {};
  for (const [key, value] of Object.entries(audio.headers ?? {})) {
    if (/^(host|connection|content-length|transfer-encoding)$/i.test(key)) continue;
    if (typeof value !== 'string' || /[\r\n]/.test(key + value)) throw new Error('Invalid audio header');
    headers[key] = value;
  }
  return { ...audio, url, headers };
}
export class AudioProxy {
  constructor(resolve) {
    this.resolve = resolve;
    this.streams = new Map();
    this.downloads = new Map();
  }
  async start() {
    this.server = http.createServer((request, response) => {
      void this.serve(request, response);
    });
    this.server.requestTimeout = 30_000;
    this.server.headersTimeout = 10_000;
    await new Promise((resolve, reject) => {
      this.server.once('error', reject);
      this.server.listen(0, '127.0.0.1', resolve);
    });
    this.port = this.server.address().port;
    this.cleanup = setInterval(() => {
      for (const [key, stream] of this.streams)
        if (stream.requests.size === 0 && Date.now() - stream.lastAccess > 30 * 60_000)
          this.release(`http://127.0.0.1/${key}`);
    }, 60_000).unref();
  }
  async open(source, id, purpose = 'play') {
    const audio = validate(await this.resolve(source, id, purpose));
    const token = randomBytes(24).toString('hex');
    this.streams.set(token, { source, id, purpose, audio, requests: new Set(), lastAccess: Date.now() });
    return {
      url: `http://127.0.0.1:${this.port}/${token}`,
      extension: audio.extension || path.extname(audio.url.pathname).slice(1) || 'mp3',
      maxBytes: Number.isSafeInteger(audio.maxBytes) && audio.maxBytes > 0 ? audio.maxBytes : undefined,
      contentTypes: audio.contentTypes,
    };
  }
  release(url) {
    let key;
    try {
      key = new URL(url).pathname.slice(1);
    } catch {
      return;
    }
    const stream = this.streams.get(key);
    if (!stream) return;
    this.streams.delete(key);
    for (const request of stream.requests) request.destroy();
  }
  revoke(extensionId) {
    for (const job of this.downloads.values())
      if (job.source.startsWith(extensionId + '/')) job.cancel.abort();
    for (const [key, stream] of this.streams)
      if (stream.source.startsWith(extensionId + '/')) this.release(`http://127.0.0.1/${key}`);
  }
  async upstream(stream, method, range, redirects = 0, audio = stream.audio) {
    if (redirects > 5) throw new Error('Too many audio redirects');
    if (
      Array.isArray(audio.allowedHosts) &&
      (audio.url.protocol !== 'https:' ||
        audio.url.username ||
        audio.url.password ||
        (audio.url.port && audio.url.port !== '443') ||
        !audio.allowedHosts.some((host) =>
          host.startsWith('*.') ? audio.url.hostname.endsWith(host.slice(1)) : audio.url.hostname === host,
        ))
    )
      throw new Error('Audio host is not allowed');
    return await new Promise((resolve, reject) => {
      const headers = { ...audio.headers };
      if (range) headers.Range = range;
      const request = (audio.url.protocol === 'https:' ? https : http).request(
        audio.url,
        { method, headers },
        (response) => {
          if ([301, 302, 303, 307, 308].includes(response.statusCode) && response.headers.location) {
            response.resume();
            const next = new URL(response.headers.location, audio.url);
            if (!['http:', 'https:'].includes(next.protocol))
              return reject(new Error('Invalid audio redirect'));
            const nextHeaders = { ...audio.headers };
            if (next.origin !== audio.url.origin)
              for (const name of Object.keys(nextHeaders))
                if (/^(authorization|cookie)$/i.test(name)) delete nextHeaders[name];
            this.upstream(stream, method, range, redirects + 1, {
              ...audio,
              url: next,
              headers: nextHeaders,
            }).then(resolve, reject);
          } else resolve(response);
        },
      );
      stream.requests.add(request);
      request.on('close', () => stream.requests.delete(request));
      request.on('error', reject);
      request.setTimeout(30_000, () => request.destroy(new Error('Audio transfer stalled')));
      request.end();
    });
  }
  async serve(request, response) {
    const key = (request.url ?? '').split('?')[0].slice(1);
    const stream = this.streams.get(key);
    if (!stream || !['GET', 'HEAD'].includes(request.method)) {
      response.writeHead(404);
      response.end();
      return;
    }
    stream.lastAccess = Date.now();
    let upstream;
    try {
      if (stream.audio.expiresAt && stream.audio.expiresAt <= Date.now() + 1000)
        stream.audio = validate(await this.resolve(stream.source, stream.id, stream.purpose));
      if (this.streams.get(key) !== stream || response.destroyed) throw new Error('Stream cancelled');
      upstream = await this.upstream(stream, request.method, request.headers.range);
      if ([401, 403, 410].includes(upstream.statusCode)) {
        upstream.destroy();
        stream.audio = validate(await this.resolve(stream.source, stream.id, stream.purpose));
        if (this.streams.get(key) !== stream || response.destroyed) throw new Error('Stream cancelled');
        upstream = await this.upstream(stream, request.method, request.headers.range);
      }
      response.on('close', () => upstream.destroy());
      const headers = {};
      for (const name of [
        'content-type',
        'content-length',
        'content-range',
        'accept-ranges',
        'etag',
        'last-modified',
      ])
        if (upstream.headers[name] !== undefined) headers[name] = upstream.headers[name];
      response.writeHead(upstream.statusCode ?? 502, headers);
      await pipeline(upstream, response);
    } catch {
      upstream?.destroy();
      if (!response.headersSent) {
        response.writeHead(502);
        response.end('Audio source unavailable');
      } else response.destroy();
    }
  }
  cancelDownload(task) {
    this.downloads.get(task)?.cancel.abort();
  }
  async download(source, id, base, task = randomUUID(), progress = () => {}) {
    const cancel = new AbortController();
    this.downloads.set(task, { source, cancel });
    let opened, temporary;
    try {
      opened = await this.open(source, id, 'download');
      const response = await fetch(opened.url, {
        signal: AbortSignal.any([cancel.signal, AbortSignal.timeout(10 * 60_000)]),
      });
      if (!response.ok || !response.body) throw new Error(`Download failed: HTTP ${response.status}`);
      const contentType = (response.headers.get('content-type') || '').split(';')[0].trim().toLowerCase();
      if (Array.isArray(opened.contentTypes) && !opened.contentTypes.includes(contentType))
        throw new Error('Response is not a supported audio type');
      const media = {
        'audio/mpeg': 'mp3',
        'audio/mp3': 'mp3',
        'audio/flac': 'flac',
        'audio/x-flac': 'flac',
        'audio/wav': 'wav',
        'audio/x-wav': 'wav',
        'audio/ogg': 'ogg',
        'application/ogg': 'ogg',
        'audio/mp4': 'm4a',
        'audio/aac': 'aac',
        'audio/webm': 'webm',
      };
      const extension =
        media[(response.headers.get('content-type') || '').split(';')[0].trim()] ||
        (/^(mp3|flac|wav|ogg|opus|m4a|aac|webm)$/i.test(opened.extension)
          ? opened.extension.toLowerCase()
          : 'mp3');
      const target = `${base}.${extension}`;
      temporary = `${target}.partial-${randomUUID()}`;
      let received = 0,
        last = 0;
      const total = Number(response.headers.get('content-length')) || 0;
      const count = new Transform({
        transform(chunk, _encoding, done) {
          received += chunk.length;
          if (opened.maxBytes && received > opened.maxBytes) {
            done(new Error('Audio exceeds source download limit'));
            return;
          }
          if (Date.now() - last > 100) {
            progress({ received, total });
            last = Date.now();
          }
          done(null, chunk);
        },
      });
      await pipeline(response.body, count, createWriteStream(temporary, { flags: 'wx', mode: 0o600 }));
      if (!received) throw new Error('Downloaded audio is empty');
      try {
        await fs.link(temporary, target);
      } catch (error) {
        if (error.code !== 'EEXIST') throw error;
      }
      return { path: target };
    } finally {
      cancel.abort();
      this.downloads.delete(task);
      if (opened) this.release(opened.url);
      if (temporary) await fs.rm(temporary, { force: true });
    }
  }
  close() {
    for (const job of this.downloads.values()) job.cancel.abort();
    clearInterval(this.cleanup);
    for (const key of [...this.streams.keys()]) this.release(`http://127.0.0.1/${key}`);
    this.server.closeAllConnections();
    this.server.close();
  }
}

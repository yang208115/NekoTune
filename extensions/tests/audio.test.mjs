import test from 'node:test';
import assert from 'node:assert/strict';
import http from 'node:http';
import fs from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import { AudioProxy } from '../runtime/audio.mjs';

test('audio proxy carries authentication/Range, refreshes expired addresses and revokes streams', async (t) => {
  const payload = Buffer.from('0123456789');
  let resolves = 0;
  const server = http.createServer((request, response) => {
    if (request.headers.authorization !== 'Bearer fixture') {
      response.writeHead(401).end();
      return;
    }
    const range = /^bytes=(\d+)-(\d+)$/.exec(request.headers.range ?? '');
    const start = range ? Number(range[1]) : 0,
      end = range ? Number(range[2]) : payload.length - 1;
    response.writeHead(range ? 206 : 200, {
      'Content-Type': 'audio/wav',
      'Content-Length': end - start + 1,
      'Accept-Ranges': 'bytes',
      ...(range ? { 'Content-Range': `bytes ${start}-${end}/${payload.length}` } : {}),
    });
    response.end(payload.subarray(start, end + 1));
  });
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  const proxy = new AudioProxy(async () => {
    resolves++;
    return {
      url: `http://127.0.0.1:${server.address().port}/audio.wav`,
      headers: { Authorization: 'Bearer fixture' },
      expiresAt: resolves === 1 ? Date.now() - 1 : Date.now() + 60_000,
      extension: 'wav',
    };
  });
  await proxy.start();
  t.after(() => {
    proxy.close();
    server.closeAllConnections();
    server.close();
  });
  const opened = await proxy.open('test.source/music', 'one');
  assert.equal(opened.url.includes('fixture'), false);
  const response = await fetch(opened.url, { headers: { Range: 'bytes=2-5' } });
  assert.equal(response.status, 206);
  assert.equal(response.headers.get('content-range'), 'bytes 2-5/10');
  assert.equal(await response.text(), '2345');
  assert.equal(resolves, 2);
  const folder = await fs.mkdtemp(path.join(os.tmpdir(), 'nekotune-download-'));
  t.after(() => fs.rm(folder, { recursive: true, force: true }));
  const downloaded = await proxy.download('test.source/music', 'one', path.join(folder, 'audio'));
  assert.equal(await fs.readFile(downloaded.path, 'utf8'), '0123456789');
  proxy.revoke('test.source');
  assert.equal((await fetch(opened.url)).status, 404);
});
test('source download limits and cancellation leave no published partial audio', async (t) => {
  const folder = await fs.mkdtemp(path.join(os.tmpdir(), 'nekotune-download-limits-'));
  t.after(() => fs.rm(folder, { recursive: true, force: true }));
  const timers = new Set();
  const server = http.createServer((request, response) => {
    response.writeHead(200, { 'Content-Type': 'audio/wav' });
    response.write(Buffer.alloc(32));
    const timer = setInterval(() => response.write(Buffer.alloc(32)), 10);
    timers.add(timer);
    response.on('close', () => {
      clearInterval(timer);
      timers.delete(timer);
    });
  });
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  let limit = 64;
  const proxy = new AudioProxy(async () => ({
    url: `http://127.0.0.1:${server.address().port}/audio.wav`,
    maxBytes: limit,
    contentTypes: ['audio/wav'],
  }));
  await proxy.start();
  t.after(() => {
    proxy.close();
    server.closeAllConnections();
    server.close();
    for (const timer of timers) clearInterval(timer);
  });
  await assert.rejects(proxy.download('test.source/music', 'one', path.join(folder, 'limited')), /limit/);
  assert.deepEqual(await fs.readdir(folder), []);
  limit = 1024 * 1024;
  const downloading = proxy.download('test.source/music', 'one', path.join(folder, 'cancelled'), 'task', () =>
    proxy.cancelDownload('task'),
  );
  await assert.rejects(downloading);
  assert.deepEqual(await fs.readdir(folder), []);
});

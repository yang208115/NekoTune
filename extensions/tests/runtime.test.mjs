import test from 'node:test';
import assert from 'node:assert/strict';
import net from 'node:net';
import fs from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { Wire } from '../runtime/wire.mjs';

async function runtime(t, safeMode = false) {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'nekotune-runtime-'));
  let wire, initial;
  const ready = new Promise((resolve) => {
    initial = resolve;
  });
  const pids = new Set();
  const server = net.createServer((socket) => {
    wire = new Wire(socket, 35_000);
    wire.on('fault', () => {});
    wire.on('message', (message) => {
      if (message.event === 'extensions.changed') initial();
      if (message.event === 'extensions.process')
        message.active ? pids.add(message.pid) : pids.delete(message.pid);
      if (message.method)
        void wire.handle(message, async (method, params) => {
          if (method === 'hello') {
            assert.equal(params.token, 'fixture');
            return {};
          }
          if (method === 'host.call') return { received: params.method, params: params.params };
          if (method === 'secret') return { value: params.operation === 'get' ? 'fixture-secret' : null };
          throw new Error(`Unexpected host method ${method}`);
        });
    });
  });
  const socket = path.join(root, 'host');
  await new Promise((resolve) => server.listen(socket, resolve));
  const child = spawn(process.execPath, [path.resolve('dist/supervisor.cjs')], {
    env: {
      ...process.env,
      NEKOTUNE_EXTENSION_SOCKET: socket,
      NEKOTUNE_EXTENSION_TOKEN: 'fixture',
      NEKOTUNE_EXTENSIONS_ROOT: path.join(root, 'extensions'),
      NEKOTUNE_SAFE_MODE: safeMode ? '1' : '0',
    },
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  let stderr = '';
  child.stderr.on('data', (bytes) => {
    stderr += bytes.toString();
  });
  const exit = once(child, 'exit');
  t.after(async () => {
    child.kill('SIGTERM');
    await Promise.race([exit, new Promise((resolve) => setTimeout(resolve, 5000))]);
    if (child.exitCode === null && child.signalCode === null) child.kill('SIGKILL');
    for (const pid of pids)
      try {
        process.kill(-pid, 'SIGKILL');
      } catch {}
    wire?.socket.destroy();
    server.close();
    await fs.rm(root, { recursive: true, force: true });
  });
  await Promise.race([
    ready,
    exit.then(() => {
      throw new Error(`Supervisor exited: ${stderr}`);
    }),
    new Promise((_, reject) =>
      setTimeout(() => reject(new Error(`Supervisor startup timeout: ${stderr}`)), 10_000).unref(),
    ),
  ]);
  return { root, call: (method, params) => wire.request(method, params), wire };
}
async function fixture(root, id, body, extra = {}) {
  const directory = path.join(root, id + '-' + Math.random().toString(16).slice(2));
  await fs.mkdir(directory);
  await fs.writeFile(path.join(directory, 'main.mjs'), body);
  await fs.writeFile(
    path.join(directory, 'extension.json'),
    JSON.stringify({ id, name: id, version: '1.0.0', apiVersion: 1, main: 'main.mjs', ...extra }),
  );
  return directory;
}
async function until(read, accept, timeout = 6000) {
  const end = Date.now() + timeout;
  while (Date.now() < end) {
    const result = await read();
    if (accept(result)) return result;
    await new Promise((resolve) => setTimeout(resolve, 40));
  }
  throw new Error('Expected state did not arrive');
}
test('lifecycle, nested calls, storage, reload and rollback', { timeout: 45_000 }, async (t) => {
  const { root, call } = await runtime(t);
  const body = `export async function activate(ctx) {
    console.log('console logging does not corrupt RPC');
    await ctx.services.register('echo', async p => ({...p, host: await ctx.host.call('player.status')}));
    await ctx.services.register('storage', async () => { await Promise.all([ctx.storage.set('one', 1), ctx.storage.set('two', 2)]); return { one: await ctx.storage.get('one'), two: await ctx.storage.get('two') }; });
    await ctx.services.register('config', async () => ctx.config);
    await ctx.services.register('secret', async () => { const value = await ctx.secrets.get('token'); console.log(value); return {}; });
    await ctx.ui.register('themes', 'test', { title: 'Dynamic theme', tokens: { accent: '#123456' } });
  }`;
  const directory = await fixture(root, 'test.echo', body);
  await call('extensions.install', { path: directory, development: true });
  await assert.rejects(call('extensions.enable', { id: 'test.echo' }), /Trust/);
  await call('extensions.enable', { id: 'test.echo', trusted: true });
  const echoed = await call('extensions.call', {
    id: 'test.echo',
    service: 'echo',
    params: { hello: 'world' },
  });
  assert.equal(echoed.hello, 'world');
  assert.equal(echoed.host.received, 'player.status');
  assert.deepEqual(await call('extensions.call', { id: 'test.echo', service: 'storage' }), {
    one: 1,
    two: 2,
  });
  await call('extensions.set_config', { id: 'test.echo', config: { delay: 10 } });
  assert.deepEqual(await call('extensions.call', { id: 'test.echo', service: 'config' }), { delay: 10 });
  await call('extensions.call', { id: 'test.echo', service: 'secret' });
  await until(
    () => call('extensions.logs', { id: 'test.echo' }),
    (result) => result.logs.some((row) => row.message.includes('[redacted]')),
  );
  assert.ok(!JSON.stringify(await call('extensions.logs', { id: 'test.echo' })).includes('fixture-secret'));
  assert.equal((await call('extensions.list')).extensions[0].contributes.themes[0].id, 'test.echo/test');
  const first = (await call('extensions.list')).extensions[0].generation;
  await call('extensions.reload', { id: 'test.echo' });
  assert.ok((await call('extensions.list')).extensions[0].generation > first);
  const manualGeneration = (await call('extensions.list')).extensions[0].generation;
  await fs.appendFile(path.join(directory, 'main.mjs'), '\n// development reload\n');
  await until(
    () => call('extensions.list'),
    (result) =>
      result.extensions[0].state === 'running' && result.extensions[0].generation > manualGeneration,
  );
  const broken = await fixture(
    root,
    'test.echo',
    'export function activate() { throw new Error("bad update"); }',
    { version: '2.0.0' },
  );
  await assert.rejects(
    call('extensions.install', { path: broken, development: true, replace: true }),
    /rolled back/,
  );
  assert.equal((await call('extensions.list')).extensions[0].version, '1.0.0');
  await call('extensions.disable', { id: 'test.echo' });
  await assert.rejects(call('extensions.call', { id: 'test.echo', service: 'echo' }), /not running/);
  assert.deepEqual((await call('extensions.list')).extensions[0].contributes, {});
  await call('extensions.install', { path: broken, development: true, replace: true });
  assert.equal((await call('extensions.list')).extensions[0].version, '2.0.0');
  await assert.rejects(call('extensions.enable', { id: 'test.echo' }), /rolled back/);
  assert.equal((await call('extensions.list')).extensions[0].version, '1.0.0');
  assert.equal((await call('extensions.list')).extensions[0].state, 'running');
  await call('extensions.disable', { id: 'test.echo' });
  await call('extensions.uninstall', { id: 'test.echo' });
  assert.equal((await call('extensions.list')).extensions.length, 0);
  assert.equal(
    JSON.parse(await fs.readFile(path.join(root, 'extensions/data/test.echo/storage.json'), 'utf8')).two,
    2,
  );
  assert.ok(await fs.stat(directory));
});
test(
  'dependency failures and independent processes survive extension crashes',
  { timeout: 25_000 },
  async (t) => {
    const { root, call } = await runtime(t);
    const healthy = await fixture(
      root,
      'test.healthy',
      `export async function activate(ctx) { await ctx.services.register('echo', async () => ({alive:true})); }`,
    );
    const broken = await fixture(
      root,
      'test.crash',
      `export async function activate(ctx) { await ctx.services.register('crash', () => process.exit(7)); }`,
    );
    for (const directory of [healthy, broken])
      await call('extensions.install', { path: directory, development: true });
    for (const id of ['test.healthy', 'test.crash']) await call('extensions.enable', { id, trusted: true });
    await assert.rejects(call('extensions.call', { id: 'test.crash', service: 'crash' }));
    await until(
      () => call('extensions.list'),
      (result) => result.extensions.find((entry) => entry.id === 'test.crash').state === 'failed',
    );
    const registry = await until(
      async () => JSON.parse(await fs.readFile(path.join(root, 'extensions/registry.json'), 'utf8')),
      (result) => result.entries.find((entry) => entry.id === 'test.crash').enabled === false,
    );
    assert.ok(registry.entries.find((entry) => entry.id === 'test.crash').failure);
    assert.deepEqual(await call('extensions.call', { id: 'test.healthy', service: 'echo' }), { alive: true });
    const dependent = await fixture(root, 'test.dependent', 'export function activate() {}', {
      dependencies: { 'test.missing': '^1.0.0' },
    });
    await call('extensions.install', { path: dependent, development: true });
    await assert.rejects(call('extensions.enable', { id: 'test.dependent', trusted: true }), /Dependency/);
  },
);
test(
  'a busy extension can be disabled while the manager remains responsive',
  { timeout: 20_000 },
  async (t) => {
    const { root, call } = await runtime(t);
    const directory = await fixture(
      root,
      'test.loop',
      `export async function activate(ctx) { await ctx.services.register('loop', () => { while(true) {} }); }`,
    );
    await call('extensions.install', { path: directory, development: true });
    await call('extensions.enable', { id: 'test.loop', trusted: true });
    const pending = call('extensions.call', { id: 'test.loop', service: 'loop' });
    const rejected = assert.rejects(pending);
    await call('extensions.disable', { id: 'test.loop' });
    await rejected;
    assert.equal((await call('extensions.list')).extensions[0].state, 'disabled');
  },
);
test('safe mode permits management but prevents code activation', { timeout: 15_000 }, async (t) => {
  const { root, call } = await runtime(t, true);
  const directory = await fixture(
    root,
    'test.safe',
    'export function activate() { throw new Error("must not execute"); }',
  );
  await call('extensions.install', { path: directory, development: true });
  await assert.rejects(call('extensions.enable', { id: 'test.safe', trusted: true }), /safe-mode/);
  assert.equal((await call('extensions.list')).safeMode, true);
});
test(
  'activation timeout disables the faulty extension and preserves a healthy process',
  { timeout: 25_000 },
  async (t) => {
    const { root, call } = await runtime(t);
    const directory = await fixture(
      root,
      'test.activation',
      'export async function activate() { await new Promise(() => {}); }',
    );
    await call('extensions.install', { path: directory, development: true });
    await assert.rejects(
      call('extensions.enable', { id: 'test.activation', trusted: true }),
      /Activation timed out/,
    );
    const entry = (await call('extensions.list')).extensions[0];
    assert.equal(entry.state, 'failed');
    assert.equal(entry.enabled, false);
    assert.deepEqual(entry.registrations, {});
  },
);

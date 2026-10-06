import test from 'node:test';
import assert from 'node:assert/strict';
import net from 'node:net';
import fs from 'node:fs/promises';
import { createWriteStream } from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import { randomUUID } from 'node:crypto';
import { pipeline } from 'node:stream/promises';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import yazl from 'yazl';
import { Wire } from '../runtime/wire.mjs';

async function runtime(t, safeMode = false, prepare = async () => {}) {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'nekotune-runtime-'));
  await prepare(root);
  let wire, initial;
  const ready = new Promise((resolve) => {
    initial = resolve;
  });
  const pids = new Set(),
    events = [];
  const server = net.createServer((socket) => {
    wire = new Wire(socket, 35_000);
    wire.on('fault', () => {});
    wire.on('message', (message) => {
      if (message.event) events.push(message);
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
  const socket =
    process.platform === 'win32' ? '\\\\.\\pipe\\' + path.basename(root) : path.join(root, 'host');
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
  return { root, call: (method, params) => wire.request(method, params), wire, events };
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
async function archive(root, id, body = 'export function activate() {}', extra = {}) {
  const directory = await fixture(root, id, body, extra);
  const output = `${directory}.zip`;
  const zip = new yazl.ZipFile();
  const writing = pipeline(zip.outputStream, createWriteStream(output));
  for (const name of await fs.readdir(directory)) zip.addFile(path.join(directory, name), name);
  zip.end();
  await writing;
  return output;
}
async function packageDirectories(root) {
  return (await fs.readdir(path.join(root, 'extensions/packages'))).sort();
}
async function until(read, accept, timeout = 6000) {
  const end = Date.now() + timeout;
  let result;
  while (Date.now() < end) {
    result = await read();
    if (accept(result)) return result;
    await new Promise((resolve) => setTimeout(resolve, 40));
  }
  throw new Error('Expected state did not arrive; last result: ' + JSON.stringify(result));
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
  'ZIP updates collect replaced and failed packages while preserving plugin data',
  { timeout: 30_000 },
  async (t) => {
    const { root, call } = await runtime(t);
    const id = 'test.packages';
    const body = 'export async function activate(ctx) { await ctx.storage.set("saved", 42); }';
    const zip = await archive(root, id, body);
    const first = (await call('extensions.install', { path: zip })).extension.directory;
    await call('extensions.enable', { id, trusted: true });
    await call('extensions.set_config', { id, config: { keep: true } });
    const current = (await call('extensions.install', { path: zip, replace: true })).extension.directory;
    assert.notEqual(first, current);
    assert.deepEqual(await packageDirectories(root), [path.basename(current)]);

    const broken = await archive(root, id, 'export function activate() { throw new Error("bad update"); }', {
      version: '2.0.0',
    });
    await assert.rejects(call('extensions.install', { path: broken, replace: true }), /rolled back/);
    assert.deepEqual(await packageDirectories(root), [path.basename(current)]);
    const entry = (await call('extensions.list')).extensions[0];
    assert.equal(entry.directory, current);
    assert.equal(entry.state, 'running');
    await call('extensions.uninstall', { id });
    assert.deepEqual(await packageDirectories(root), []);
    const data = path.join(root, 'extensions/data', id);
    assert.deepEqual(JSON.parse(await fs.readFile(path.join(data, 'config.json'), 'utf8')), { keep: true });
    assert.deepEqual(JSON.parse(await fs.readFile(path.join(data, 'storage.json'), 'utf8')), { saved: 42 });
  },
);

test(
  'disabled updates retain only the rollback package until activation or uninstall',
  { timeout: 30_000 },
  async (t) => {
    const { root, call } = await runtime(t);
    const id = 'test.delayed';
    const good = await archive(root, id);
    const first = (await call('extensions.install', { path: good })).extension.directory;
    await call('extensions.enable', { id, trusted: true });
    await call('extensions.disable', { id });
    await call('extensions.install', { path: good, replace: true });
    const bad = await archive(root, id, 'export function activate() { throw new Error("bad update"); }', {
      version: '2.0.0',
    });
    const failed = (await call('extensions.install', { path: bad, replace: true })).extension.directory;
    assert.deepEqual(
      await packageDirectories(root),
      [first, failed].map((directory) => path.basename(directory)).sort(),
    );
    await assert.rejects(call('extensions.enable', { id }), /rolled back/);
    assert.deepEqual(await packageDirectories(root), [path.basename(first)]);
    assert.equal((await call('extensions.list')).extensions[0].directory, first);

    await call('extensions.disable', { id });
    const current = (await call('extensions.install', { path: good, replace: true })).extension.directory;
    await call('extensions.enable', { id });
    assert.deepEqual(await packageDirectories(root), [path.basename(current)]);
    await call('extensions.disable', { id });
    await call('extensions.install', { path: good, replace: true });
    assert.equal((await packageDirectories(root)).length, 2);
    await call('extensions.uninstall', { id, clearData: true });
    assert.deepEqual(await packageDirectories(root), []);
    await assert.rejects(fs.stat(path.join(root, 'extensions/data', id)), { code: 'ENOENT' });
  },
);

test(
  'dependency restart failure keeps the old package available for update rollback',
  { timeout: 30_000 },
  async (t) => {
    const { root, call } = await runtime(t);
    const id = 'test.provider';
    const zip = await archive(root, id);
    const first = (await call('extensions.install', { path: zip })).extension.directory;
    await call('extensions.enable', { id, trusted: true });
    const dependent = await archive(root, 'test.consumer', undefined, { dependencies: { [id]: '^1.0.0' } });
    const other = (await call('extensions.install', { path: dependent })).extension.directory;
    await call('extensions.enable', { id: 'test.consumer', trusted: true });
    const update = await archive(root, id, undefined, { version: '2.0.0' });
    await assert.rejects(call('extensions.install', { path: update, replace: true }), /rolled back/);
    assert.deepEqual(
      await packageDirectories(root),
      [first, other].map((directory) => path.basename(directory)).sort(),
    );
    const entries = (await call('extensions.list')).extensions;
    assert.ok(entries.every((entry) => entry.state === 'running'));
    assert.equal(entries.find((entry) => entry.id === id).directory, first);
  },
);

test(
  'startup collects historical packages and staging directories but preserves registered and development files',
  { timeout: 15_000 },
  async (t) => {
    let retained;
    const { root, call } = await runtime(t, true, async (root) => {
      const packages = path.join(root, 'extensions/packages');
      await fs.mkdir(packages, { recursive: true });
      retained = ['test.current', 'test.previous', 'test.development'].map(
        (id) => `${id}-1.0.0-${randomUUID()}`,
      );
      const obsolete = `test.obsolete-1.0.0-${randomUUID()}`;
      for (const name of [...retained, obsolete, `.staging-${randomUUID()}`, 'personal-notes'])
        await fs.mkdir(path.join(packages, name));
      // Even an unreadable current manifest must not turn its package into garbage.
      await fs.writeFile(
        path.join(root, 'extensions/registry.json'),
        JSON.stringify({
          entries: [
            {
              id: 'test.current',
              directory: path.join(packages, retained[0]),
              previous: { directory: path.join(packages, retained[1]) },
            },
          ],
          developmentDirectories: [path.join(packages, retained[2])],
          selections: {},
        }),
      );
    });
    await until(
      () => packageDirectories(root),
      (names) => names.length === 4,
    );
    assert.deepEqual(await packageDirectories(root), [...retained, 'personal-notes'].sort());
    assert.equal((await call('extensions.list')).extensions[0].state, 'failed');
  },
);

test(
  'uninstall preserves development originals even when mounted from the packages directory',
  { timeout: 15_000 },
  async (t) => {
    const { root, call } = await runtime(t);
    const id = 'test.development';
    const original = await fixture(root, id, 'export function activate() {}');
    const directory = path.join(root, 'extensions/packages', `${id}-1.0.0-${randomUUID()}`);
    await fs.mkdir(path.dirname(directory), { recursive: true });
    await fs.rename(original, directory);
    await call('extensions.install', { path: directory, development: true });
    await call('extensions.uninstall', { id });
    assert.ok(await fs.stat(path.join(directory, 'main.mjs')));
    const registry = JSON.parse(await fs.readFile(path.join(root, 'extensions/registry.json'), 'utf8'));
    assert.ok(registry.developmentDirectories.includes(directory));
  },
);

test(
  'loaded native packages are retained after uninstall until the next application session',
  { timeout: 15_000 },
  async (t) => {
    const { root, call } = await runtime(t);
    const id = 'test.native';
    const zip = await archive(root, id, undefined, { nativeModules: true });
    const directory = (await call('extensions.install', { path: zip })).extension.directory;
    await call('extensions.enable', { id, trusted: true });
    await call('extensions.uninstall', { id });
    assert.deepEqual(await packageDirectories(root), [path.basename(directory)]);
    assert.deepEqual((await call('extensions.list')).extensions, []);
  },
);

test(
  'registry save failures prevent cleanup from destroying rollback packages',
  { timeout: 30_000 },
  async (t) => {
    const { root, call, events } = await runtime(t);
    const id = 'test.persistence';
    const zip = await archive(root, id);
    const directory = (await call('extensions.install', { path: zip })).extension.directory;
    await call('extensions.enable', { id, trusted: true });
    const registry = path.join(root, 'extensions/registry.json');
    const saved = await fs.readFile(registry);
    await fs.rm(registry);
    await fs.mkdir(registry);
    await assert.rejects(call('extensions.install', { path: zip, replace: true }));
    assert.ok(await fs.stat(path.join(directory, 'main.mjs')));
    assert.equal((await packageDirectories(root)).length, 2);
    assert.ok(
      events.some((event) => event.event === 'extensions.error' && event.message.includes('cleanup skipped')),
    );
    await fs.rmdir(registry);
    await fs.writeFile(registry, saved);
    await call('extensions.disable', { id });
    assert.deepEqual(await packageDirectories(root), [path.basename(directory)]);
  },
);

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
    ).catch(async (error) => {
      error.message +=
        '\nSupervisor logs: ' + JSON.stringify(await call('extensions.logs', { id: 'test.crash' }));
      throw error;
    });
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

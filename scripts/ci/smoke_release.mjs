// Run the installed desktop app with isolated data and no build-tool search paths.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { randomUUID } from 'node:crypto';
import fs from 'node:fs/promises';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';

const stage = path.resolve(process.argv[2]);
const root = await fs.mkdtemp(path.join(os.tmpdir(), 'nekotune-release-'));
const socketName = process.platform === 'win32'
  ? 'nekotune-release-' + randomUUID() : path.join(root, 'backend.sock');
const endpoint = process.platform === 'win32' ? '\\\\.\\pipe\\' + socketName : socketName;
const environment = { ...process.env };
for (const key of ['LD_LIBRARY_PATH', 'QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH',
  'QML_IMPORT_PATH', 'QML2_IMPORT_PATH', 'QT_DIR', 'QTDIR']) delete environment[key];
for (const key of Object.keys(environment)) {
  if (key.toLowerCase() === 'path') delete environment[key];
}
Object.assign(environment, {
  NEKOTUNE_HOME: root, NEKOTUNE_SOCKET: socketName,
  XDG_CONFIG_HOME: path.join(root, 'config'), XDG_DATA_HOME: path.join(root, 'data'),
  XDG_CACHE_HOME: path.join(root, 'cache'),
  QT_QPA_PLATFORM: process.platform === 'win32' ? 'windows' : 'offscreen',
  QT_QUICK_BACKEND: 'software',
  PATH: process.platform === 'win32'
    ? `${stage}/bin;${process.env.SystemRoot}/System32;${process.env.SystemRoot}`
    : '/usr/bin:/bin',
});
const executable = process.platform === 'win32'
  ? path.join(stage, 'bin', 'nekotune.exe') : path.join(stage, 'NekoTune');
const child = spawn(executable, ['--safe-mode'], { env: environment, windowsHide: true });
let output = '', exited = false;
child.stdout.on('data', bytes => { output += bytes; });
child.stderr.on('data', bytes => { output += bytes; });
child.on('exit', () => { exited = true; });
child.on('error', error => { output += error.stack; exited = true; });
let socket;
try {
  const deadline = Date.now() + 45_000;
  while (Date.now() < deadline) {
    assert.ok(!exited, `Desktop exited during startup:\n${output}`);
    socket = net.createConnection(endpoint);
    if (await new Promise(resolve => {
      socket.once('connect', () => resolve(true));
      socket.once('error', () => resolve(false));
    })) break;
    socket.destroy();
    await new Promise(resolve => setTimeout(resolve, 150));
  }
  assert.ok(socket && !socket.destroyed, `Desktop IPC startup timed out:\n${output}`);
  let buffer = '', id = 0;
  const pending = new Map();
  socket.on('data', bytes => {
    buffer += bytes;
    while (buffer.includes('\n')) {
      const newline = buffer.indexOf('\n');
      const message = JSON.parse(buffer.slice(0, newline));
      buffer = buffer.slice(newline + 1);
      const resolve = pending.get(message.id);
      if (resolve) { pending.delete(message.id); resolve(message); }
    }
  });
  async function call(method) {
    const requestId = ++id;
    const reply = new Promise(resolve => pending.set(requestId, resolve));
    socket.write(JSON.stringify({ id: requestId, method, params: {} }) + '\n');
    let timer;
    try {
      const response = await Promise.race([reply, new Promise((_, reject) => {
        timer = setTimeout(() => reject(new Error(`IPC timeout: ${method}\n${output}`)), 5000);
      })]);
      assert.equal(response.status, 'ok', JSON.stringify(response));
      return response.data;
    } finally { clearTimeout(timer); }
  }
  assert.ok(await call('player.status'));
  let extensions;
  while (Date.now() < deadline) {
    extensions = await call('extensions.list');
    if (extensions.catalogueReady) break;
    await new Promise(resolve => setTimeout(resolve, 150));
  }
  assert.equal(extensions?.catalogueReady, true, `Bundled Node supervisor failed:\n${output}`);
  await new Promise(resolve => setTimeout(resolve, 1500));
  assert.ok(!exited, `Desktop exited after IPC startup:\n${output}`);
  assert.doesNotMatch(output, /QQmlApplicationEngine failed|module "[^"]+" is not installed/);
  console.log('Installed desktop, QML, SQLite, IPC and bundled Node supervisor passed.');
} finally {
  socket?.destroy();
  child.kill();
  if (!exited) await Promise.race([
    new Promise(resolve => child.once('exit', resolve)),
    new Promise(resolve => setTimeout(resolve, 5000)),
  ]);
  if (!exited) child.kill('SIGKILL');
  await fs.rm(root, { recursive: true, force: true, maxRetries: 20, retryDelay: 150 });
}

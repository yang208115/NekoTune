import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import { createWriteStream } from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import { randomUUID } from 'node:crypto';
import { pipeline } from 'node:stream/promises';
import yazl from 'yazl';
import { readManifest, unpack, dependencyOrder, writeJson, cleanupPackages } from '../runtime/packages.mjs';

test('atomic JSON replacement retries sharing failures and preserves existing data on permanent failure', async (t) => {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'nekotune-json-'));
  t.after(() => fs.rm(root, { recursive: true, force: true }));
  const file = path.join(root, 'registry.json');
  await writeJson(file, { enabled: true });
  const rename = fs.rename;
  let attempts = 0;
  const mocked = t.mock.method(fs, 'rename', async (...args) => {
    if (++attempts <= 2) {
      assert.deepEqual(JSON.parse(await fs.readFile(file, 'utf8')), { enabled: true });
      throw Object.assign(new Error('Destination is temporarily open'), { code: 'EPERM' });
    }
    return rename(...args);
  });
  await writeJson(file, { enabled: false, failure: 'Extension disconnected' });
  assert.equal(attempts, 3);
  const saved = JSON.parse(await fs.readFile(file, 'utf8'));
  assert.equal(saved.enabled, false);
  assert.ok(saved.failure);
  mocked.mock.mockImplementation(async () => {
    throw Object.assign(new Error('Invalid destination'), { code: 'EINVAL' });
  });
  await assert.rejects(writeJson(file, { enabled: true }), { code: 'EINVAL' });
  assert.deepEqual(JSON.parse(await fs.readFile(file, 'utf8')), saved);
  attempts = 0;
  mocked.mock.mockImplementation(async () => {
    attempts++;
    throw Object.assign(new Error('Destination remains busy'), { code: 'EBUSY' });
  });
  await assert.rejects(writeJson(file, { enabled: true }), { code: 'EBUSY' });
  assert.equal(attempts, 7);
  assert.deepEqual(JSON.parse(await fs.readFile(file, 'utf8')), saved);
  assert.deepEqual(await fs.readdir(root), ['registry.json']);
});

test('manifest entry containment and dependency versions/cycles', async (t) => {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'nekotune-manifest-'));
  t.after(() => fs.rm(root, { recursive: true, force: true }));
  await fs.writeFile(path.join(root, 'main.mjs'), 'export function activate() {}');
  const manifest = { id: 'test.one', name: 'One', version: '1.2.0', apiVersion: 1, main: 'main.mjs' };
  await fs.writeFile(path.join(root, 'extension.json'), JSON.stringify(manifest));
  assert.equal((await readManifest(root)).id, 'test.one');
  await fs.writeFile(
    path.join(root, 'extension.json'),
    JSON.stringify({ ...manifest, main: '../outside.mjs' }),
  );
  await assert.rejects(readManifest(root));
  const entries = new Map([
    ['test.one', { manifest }],
    ['test.two', { manifest: { ...manifest, id: 'test.two', dependencies: { 'test.one': '^1.0.0' } } }],
  ]);
  assert.deepEqual(dependencyOrder(entries, 'test.two'), ['test.one', 'test.two']);
  manifest.dependencies = { 'test.two': '*' };
  assert.throws(() => dependencyOrder(entries, 'test.two'), /cycle/);
  manifest.dependencies = {};
  entries.get('test.two').manifest.dependencies['test.one'] = '^2';
  assert.throws(() => dependencyOrder(entries, 'test.two'), /satisfy/);
});
test('ZIP installation extracts files and rejects symlinks', async (t) => {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'nekotune-archive-'));
  t.after(() => fs.rm(root, { recursive: true, force: true }));
  for (const symlink of [false, true]) {
    const zip = new yazl.ZipFile(),
      output = path.join(root, `${symlink}.zip`);
    const writing = pipeline(zip.outputStream, createWriteStream(output));
    zip.addBuffer(Buffer.from('hello'), 'entry.txt', { mode: symlink ? 0o120777 : 0o100644 });
    zip.end();
    await writing;
    const destination = path.join(root, `files-${symlink}`);
    if (symlink) await assert.rejects(unpack(output, destination), /Unsafe/);
    else {
      await unpack(output, destination);
      assert.equal(await fs.readFile(path.join(destination, 'entry.txt'), 'utf8'), 'hello');
    }
  }
});

test('package cleanup respects references, ownership and symlink boundaries', async (t) => {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'nekotune-cleanup-'));
  t.after(() => fs.rm(root, { recursive: true, force: true }));
  const packages = path.join(root, 'packages');
  await fs.mkdir(packages);
  const names = ['retired', 'current', 'nested', 'linked', 'symlink'].map(
    (id) => `test.${id}-1.2.3-beta.1+build.5-${randomUUID()}`,
  );
  const [retired, current, nested, linked, symlink] = names.map((name) => path.join(packages, name));
  const external = path.join(root, 'external');
  const alias = path.join(root, 'development-link');
  for (const directory of [retired, current, nested, linked, external]) await fs.mkdir(directory);
  await fs.mkdir(path.join(nested, 'development'));
  await fs.writeFile(path.join(external, 'keep.txt'), 'untouched');
  await fs.symlink(external, symlink, 'junction');
  await fs.symlink(external, path.join(retired, 'external-link'), 'junction');
  await fs.symlink(linked, alias, 'junction');
  const unrelated = [
    'notes',
    'test.retired-1.0.0-manual',
    `.staging-manual`,
    `test.bad-not-a-version-${randomUUID()}`,
  ];
  for (const name of unrelated) await fs.mkdir(path.join(packages, name));
  await fs.mkdir(path.join(packages, `.staging-${randomUUID()}`));
  assert.deepEqual(await cleanupPackages(packages, [current, path.join(nested, 'development'), alias]), []);
  assert.deepEqual((await fs.readdir(packages)).sort(), [...names.slice(1), ...unrelated].sort());
  assert.equal(await fs.readFile(path.join(external, 'keep.txt'), 'utf8'), 'untouched');
  assert.ok((await fs.lstat(symlink)).isSymbolicLink());

  const rootLink = path.join(root, 'packages-link');
  await fs.symlink(packages, rootLink, 'junction');
  await assert.rejects(cleanupPackages(rootLink, []), /real directory/);
  assert.ok(await fs.stat(current));
});

test('failed package deletion is reported and can be retried without blocking other cleanup', async (t) => {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'nekotune-cleanup-'));
  t.after(() => fs.rm(root, { recursive: true, force: true }));
  const blocked = path.join(root, `test.blocked-1.0.0-${randomUUID()}`);
  const retired = path.join(root, `test.retired-1.0.0-${randomUUID()}`);
  await fs.mkdir(blocked);
  await fs.mkdir(retired);
  const remove = fs.rm;
  const mocked = t.mock.method(fs, 'rm', async (target, options) => {
    if (target === blocked) throw Object.assign(new Error('Package remains busy'), { code: 'EBUSY' });
    return remove(target, options);
  });
  const failures = await cleanupPackages(root, []);
  assert.equal(failures.length, 1);
  assert.equal(failures[0].directory, blocked);
  assert.equal(failures[0].error.code, 'EBUSY');
  assert.deepEqual(await fs.readdir(root), [path.basename(blocked)]);
  mocked.mock.restore();
  assert.deepEqual(await cleanupPackages(root, []), []);
  assert.deepEqual(await fs.readdir(root), []);
});

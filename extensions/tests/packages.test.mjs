import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import { createWriteStream } from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import { pipeline } from 'node:stream/promises';
import yazl from 'yazl';
import { readManifest, unpack, dependencyOrder } from '../runtime/packages.mjs';

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

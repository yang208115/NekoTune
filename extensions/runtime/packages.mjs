import fs from 'node:fs/promises';
import { createWriteStream } from 'node:fs';
import path from 'node:path';
import { pipeline } from 'node:stream/promises';
import { randomUUID } from 'node:crypto';
import { setTimeout as delay } from 'node:timers/promises';
import semver from 'semver';
import yauzl from 'yauzl';

export const validId = (id) => typeof id === 'string' && /^[a-z][a-z0-9._-]{1,100}$/.test(id);
export async function readManifest(directory) {
  const manifestPath = path.join(directory, 'extension.json');
  if ((await fs.stat(manifestPath)).size > 256 * 1024) throw new Error('Manifest is too large');
  const manifest = JSON.parse(await fs.readFile(manifestPath, 'utf8'));
  if (
    !validId(manifest.id) ||
    !semver.valid(manifest.version) ||
    manifest.apiVersion !== 1 ||
    typeof manifest.name !== 'string'
  )
    throw new Error('Invalid extension identity, version or apiVersion (expected 1)');
  if (manifest.main) await containedFile(directory, manifest.main);
  if (!manifest.main && !manifest.contributes)
    throw new Error('Extension needs a main entry or UI contributions');
  for (const [id, range] of Object.entries(manifest.dependencies ?? {}))
    if (!validId(id) || !semver.validRange(range)) throw new Error(`Invalid dependency: ${id}`);
  const contributions = manifest.contributes ?? {};
  for (const kind of ['pages', 'settings', 'slots', 'themes', 'menus', 'toolbars']) {
    if (contributions[kind] !== undefined && !Array.isArray(contributions[kind]))
      throw new Error(`contributes.${kind} must be an array`);
    const ids = new Set();
    for (const item of contributions[kind] ?? []) {
      if (!validId(item.id) || ids.has(item.id))
        throw new Error(`Invalid/duplicate contribution: ${kind}/${item.id}`);
      ids.add(item.id);
      if (['pages', 'settings', 'slots', 'toolbars'].includes(kind))
        await containedFile(directory, item.source);
    }
  }
  return manifest;
}
export async function containedFile(directory, relative) {
  if (typeof relative !== 'string' || !relative || path.isAbsolute(relative))
    throw new Error('Entry must be a relative file');
  const root = await fs.realpath(directory),
    file = await fs.realpath(path.resolve(root, relative));
  if (!file.startsWith(root + path.sep) || !(await fs.stat(file)).isFile())
    throw new Error('Entry escapes extension directory');
  return file;
}
export async function writeJson(file, value) {
  await fs.mkdir(path.dirname(file), { recursive: true, mode: 0o700 });
  const temporary = `${file}.${randomUUID()}.tmp`;
  try {
    await fs.writeFile(temporary, JSON.stringify(value, null, 2) + '\n', { mode: 0o600 });
    // Readers and virus scanners can briefly block replacement on Windows.
    // Keep the old file intact and retry only these bounded sharing failures.
    for (let attempt = 0; ; attempt++) {
      try {
        await fs.rename(temporary, file);
        break;
      } catch (error) {
        if (!['EACCES', 'EPERM', 'EBUSY'].includes(error.code) || attempt === 6) throw error;
        await delay(25 * 2 ** attempt);
      }
    }
  } finally {
    await fs.rm(temporary, { force: true });
  }
}
export async function readJson(file, fallback) {
  try {
    return JSON.parse(await fs.readFile(file, 'utf8'));
  } catch (error) {
    if (error.code === 'ENOENT') return fallback;
    throw error;
  }
}
export async function cleanupPackages(directory, retainedDirectories) {
  const rootStat = await fs.lstat(directory).catch((error) => {
    if (error.code !== 'ENOENT') throw error;
  });
  if (!rootStat) return [];
  if (!rootStat.isDirectory()) throw new Error('Package cleanup requires a real directory');
  const root = await fs.realpath(directory);
  const retained = new Set();
  for (const reference of retainedDirectories) {
    // Keep both spellings: a development directory may point into a package via a symlink.
    retained.add(path.resolve(reference));
    try {
      retained.add(await fs.realpath(reference));
    } catch (error) {
      if (error.code !== 'ENOENT') throw error;
    }
  }
  const uuid = '[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}';
  const stagingName = new RegExp(`^\\.staging-${uuid}$`);
  const packageName = new RegExp(`^([a-z][a-z0-9._-]{1,100})-(v?\\d+\\.\\d+\\.\\d+[^/]*)-${uuid}$`);
  const failures = [];
  for (const item of await fs.readdir(root, { withFileTypes: true })) {
    if (!item.isDirectory()) continue;
    const match = item.name.match(packageName);
    if (!stagingName.test(item.name) && !(match && semver.valid(match[2]))) continue;
    const target = path.join(root, item.name);
    if (
      [...retained].some(
        (reference) =>
          reference === target ||
          reference.startsWith(target + path.sep) ||
          target.startsWith(reference + path.sep),
      )
    )
      continue;
    try {
      // Never follow directory links, including links inside a retired package.
      if (!(await fs.lstat(target)).isDirectory()) continue;
      await fs.rm(target, { recursive: true, force: true, maxRetries: 2, retryDelay: 50 });
    } catch (error) {
      failures.push({ directory: target, error });
    }
  }
  return failures;
}
export async function unpack(zipPath, destination) {
  const zip = await new Promise((resolve, reject) =>
    yauzl.open(zipPath, { lazyEntries: true }, (error, file) => (error ? reject(error) : resolve(file))),
  );
  let bytes = 0,
    files = 0;
  await fs.mkdir(destination, { recursive: true, mode: 0o700 });
  return new Promise((resolve, reject) => {
    let failed = false;
    const fail = (error) => {
      if (failed) return;
      failed = true;
      zip.close();
      reject(error);
    };
    zip.on('error', fail);
    zip.on('end', resolve);
    zip.on('entry', async (entry) => {
      try {
        bytes += entry.uncompressedSize;
        files++;
        const unixType = (entry.externalFileAttributes >>> 16) & 0o170000;
        const name = entry.fileName;
        if (
          files > 20_000 ||
          bytes > 512 * 1024 * 1024 ||
          name.includes('\\') ||
          path.isAbsolute(name) ||
          name.split('/').includes('..') ||
          (unixType && unixType !== 0o100000 && unixType !== 0o040000)
        )
          throw new Error('Unsafe or oversized extension archive');
        const target = path.resolve(destination, name);
        if (!target.startsWith(path.resolve(destination) + path.sep))
          throw new Error('Archive path escapes destination');
        if (name.endsWith('/')) await fs.mkdir(target, { recursive: true, mode: 0o700 });
        else {
          await fs.mkdir(path.dirname(target), { recursive: true, mode: 0o700 });
          const stream = await new Promise((resolve, reject) =>
            zip.openReadStream(entry, (error, stream) => (error ? reject(error) : resolve(stream))),
          );
          await pipeline(
            stream,
            createWriteStream(target, {
              flags: 'wx',
              mode: (entry.externalFileAttributes >>> 16) & 0o111 ? 0o700 : 0o600,
            }),
          );
        }
        if (!failed) zip.readEntry();
      } catch (error) {
        fail(error);
      }
    });
    zip.readEntry();
  });
}
export function dependencyOrder(entries, id, visiting = new Set(), result = []) {
  if (visiting.has(id)) throw new Error(`Dependency cycle: ${[...visiting, id].join(' → ')}`);
  if (result.includes(id)) return result;
  const entry = entries.get(id);
  if (!entry) throw new Error(`Missing extension: ${id}`);
  visiting.add(id);
  for (const [dependency, range] of Object.entries(entry.manifest.dependencies ?? {})) {
    const other = entries.get(dependency);
    if (!other || !semver.satisfies(other.manifest.version, range))
      throw new Error(`Dependency ${dependency} must satisfy ${range}`);
    dependencyOrder(entries, dependency, visiting, result);
  }
  visiting.delete(id);
  result.push(id);
  return result;
}

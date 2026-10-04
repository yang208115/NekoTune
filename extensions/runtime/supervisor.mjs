import net from 'node:net';
import fs from 'node:fs/promises';
import { watch } from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import { randomBytes, randomUUID } from 'node:crypto';
import { spawn } from 'node:child_process';
import { Wire } from './wire.mjs';
import {
  readManifest,
  containedFile,
  writeJson,
  readJson,
  unpack,
  dependencyOrder,
  validId,
} from './packages.mjs';
import { AudioProxy } from './audio.mjs';

async function main() {
  const root = process.env.NEKOTUNE_EXTENSIONS_ROOT;
  const stateFile = path.join(root, 'registry.json');
  const entries = new Map(),
    runs = new Map(),
    logs = new Map(),
    secrets = new Set(),
    nativeActivated = new Set();
  const uiKinds = ['pages', 'settings', 'slots', 'themes', 'menus', 'toolbars'];
  const runtimeDirectory = path.dirname(process.argv[1]);
  let state = { entries: [], selections: {} },
    closing = false,
    catalogueReady = false,
    generation = 0,
    mutations = Promise.resolve();
  const host = new Wire(net.createConnection(process.env.NEKOTUNE_EXTENSION_SOCKET));
  const audioProxy = new AudioProxy(async (source, id, purpose) =>
    dispatch('extensions.provider_call', {
      source,
      kind: 'music',
      operation: purpose === 'download' ? 'download' : 'resolve',
      params: { id, purpose },
    }),
  );
  await audioProxy.start();
  host.on('fault', (error) => console.error(error.message));
  host.on('close', () => {
    void shutdown().finally(() => process.exit(0));
  });
  const socketDirectory = await fs.mkdtemp(path.join(os.tmpdir(), 'nekotune-ext-'));
  await fs.chmod(socketDirectory, 0o700);
  const socketPath = process.platform === 'win32'
    ? '\\\\.\\pipe\\nekotune-ext-' + randomUUID()
    : path.join(socketDirectory, 'rpc');
  const server = net.createServer((socket) => {
    const wire = new Wire(socket);
    let run;
    const authenticate = setTimeout(() => socket.destroy(), 3000);
    wire.on('fault', () => {});
    wire.on('message', (message) => {
      void wire.handle(message, async (method, params) => {
        if (!run) {
          if (method !== 'hello') throw new Error('Handshake required');
          const candidate = runs.get(params.id);
          if (!candidate || candidate.token !== params.token || candidate.wire)
            throw new Error('Invalid extension handshake');
          clearTimeout(authenticate);
          run = candidate;
          run.wire = wire;
          const entry = entries.get(run.id);
          run.storage = await readJson(path.join(dataDirectory(run.id), 'storage.json'), {});
          run.secretKeys = new Set(await readJson(path.join(dataDirectory(run.id), 'secret-keys.json'), []));
          return {
            manifest: entry.manifest,
            directory: entry.directory,
            dataDirectory: dataDirectory(run.id),
            config: await readJson(
              path.join(dataDirectory(run.id), 'config.json'),
              entry.manifest.config ?? {},
            ),
          };
        }
        if (
          runs.get(run.id) !== run ||
          (run.stopping &&
            !['unregister', 'event', 'storage.get', 'storage.set', 'secret', 'config', 'host.call'].includes(
              method,
            ))
        )
          throw new Error('Extension generation is no longer active');
        if (method === 'ready') {
          run.state = 'running';
          clearTimeout(run.startTimer);
          run.resolve();
          changed();
          return {};
        }
        if (method === 'activation.failed') {
          run.reject(new Error(params.message || 'Activation failed'));
          return {};
        }
        if (method === 'register' || method === 'unregister') {
          if (
            !['services', 'commands', 'music', 'lyrics', ...uiKinds].includes(params.kind) ||
            !validId(params.id)
          )
            throw new Error('Invalid registration');
          const registry = run.registrations[params.kind];
          if (method === 'register') {
            if (
              registry.has(params.id) ||
              (entries.get(run.id).manifest.contributes?.[params.kind] ?? []).some(
                (item) => item.id === params.id,
              )
            )
              throw new Error('Duplicate registration');
            if (['pages', 'settings', 'slots', 'toolbars'].includes(params.kind))
              await containedFile(entries.get(run.id).directory, params.descriptor?.source);
            registry.set(params.id, {
              ...params.descriptor,
              id: `${run.id}/${params.id}`,
              localId: params.id,
              extensionId: run.id,
            });
          } else registry.delete(params.id);
          changed();
          return {};
        }
        if (method === 'host.call') {
          if (
            run.state !== 'running' &&
            /^extensions\.(install|enable|disable|reload|uninstall)$/.test(params.method)
          )
            throw new Error('Defer lifecycle mutations until activation has completed');
          if (params.method === 'extensions.call' && params.params?.id === run.id)
            return await invoke(params.params);
          return await host.request('host.call', { ...params, extensionId: run.id });
        }
        if (method === 'secret') {
          if (typeof params.key !== 'string' || !params.key || params.key.length > 200)
            throw new Error('Invalid secret key');
          if (
            !['get', 'set', 'delete'].includes(params.operation) ||
            (params.operation === 'set' && typeof params.value !== 'string')
          )
            throw new Error('Invalid secret operation/value');
          if (typeof params.value === 'string' && params.value) secrets.add(params.value);
          const keysFile = path.join(dataDirectory(run.id), 'secret-keys.json');
          const keys = run.secretKeys;
          const saveKeys = () => {
            run.secretWrites = (run.secretWrites ?? Promise.resolve())
              .catch(() => {})
              .then(() => writeJson(keysFile, [...keys]));
            return run.secretWrites;
          };
          if (params.operation === 'set' || params.operation === 'get') {
            keys.add(params.key);
            await saveKeys();
          }
          const result = await host.request('secret', { ...params, extensionId: run.id });
          if (params.operation === 'delete') {
            keys.delete(params.key);
            await saveKeys();
          }
          if (typeof result.value === 'string' && result.value) secrets.add(result.value);
          return result;
        }
        if (method === 'storage.get' || method === 'storage.set') {
          if (typeof params.key !== 'string' || params.key.length > 256)
            throw new Error('Invalid storage key');
          const file = path.join(dataDirectory(run.id), 'storage.json');
          if (method === 'storage.get') {
            await run.storageWrites?.catch(() => {});
            return { value: run.storage[params.key] ?? null };
          }
          run.storageWrites = (run.storageWrites ?? Promise.resolve())
            .catch(() => {})
            .then(async () => {
              const next = { ...run.storage, [params.key]: params.value };
              if (Buffer.byteLength(JSON.stringify(next)) > 8 * 1024 * 1024)
                throw new Error('SDK storage limit exceeded; use dataDirectory for larger files');
              await writeJson(file, next);
              run.storage = next;
            });
          await run.storageWrites;
          return {};
        }
        if (method === 'config') return await setConfig({ id: run.id, config: params.config });
        if (method === 'event') {
          if (!validId(params.event)) throw new Error('Invalid event name');
          const event = {
            event: `extension.${run.id}.${params.event}`,
            extensionId: run.id,
            data: params.data ?? {},
          };
          broadcast(event);
          host.send(event);
          return {};
        }
        throw new Error(`Unknown SDK operation: ${method}`);
      });
    });
    wire.on('close', () => {
      clearTimeout(authenticate);
      if (run && runs.get(run.id) === run && !run.stopping)
        fault(run.id, new Error('Extension disconnected'));
    });
  });
  await new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(socketPath, resolve);
  });

  function dataDirectory(id) {
    return path.join(root, 'data', id);
  }
  function redact(message) {
    message = String(message);
    for (const secret of secrets) message = message.split(secret).join('[redacted]');
    return message;
  }
  function recordLog(id, level, message) {
    message = redact(message).slice(0, 16_384);
    const list = logs.get(id) ?? [];
    list.push({ time: new Date().toISOString(), level, message });
    if (list.length > 500) list.splice(0, list.length - 500);
    logs.set(id, list);
  }
  function publicEntry(entry) {
    const run = runs.get(entry.manifest.id),
      contributions = structuredClone(entry.manifest.contributes ?? {});
    if (run)
      for (const kind of uiKinds)
        contributions[kind] = [
          ...(contributions[kind] ?? []),
          ...structuredClone([...run.registrations[kind].values()]),
        ];
    for (const list of Object.values(contributions))
      if (Array.isArray(list))
        for (const item of list) {
          item.localId ??= item.id;
          item.id = `${entry.manifest.id}/${item.localId}`;
          item.extensionId = entry.manifest.id;
          if (item.source) item.source = path.join(entry.directory, item.source);
        }
    return {
      id: entry.manifest.id,
      name: entry.manifest.name,
      version: entry.manifest.version,
      description: entry.manifest.description ?? '',
      directory: entry.directory,
      development: entry.development,
      trusted: entry.trusted,
      enabled: entry.enabled,
      state: run?.state ?? entry.state ?? 'disabled',
      error: redact(entry.error ?? ''),
      generation: run?.generation ?? 0,
      nativeModules: Boolean(entry.manifest.nativeModules),
      contributes: run?.state === 'running' ? contributions : {},
      registrations:
        run?.state === 'running'
          ? Object.fromEntries(
              Object.entries(run.registrations).map(([key, registry]) => [key, [...registry.values()]]),
            )
          : {},
    };
  }
  function snapshot() {
    return {
      extensions: [...entries.values()].map(publicEntry),
      selections: state.selections ?? {},
      safeMode: process.env.NEKOTUNE_SAFE_MODE === '1',
      catalogueReady,
    };
  }
  function changed() {
    if (!closing) host.send({ event: 'extensions.changed', ...snapshot() });
  }
  async function save() {
    state.entries = [...entries.values()].map(
      ({ directory, development, enabled, trusted, manifest, previous, state: currentState, error }) => ({
        id: manifest.id,
        directory,
        development,
        enabled,
        trusted,
        previous,
        failure: currentState === 'failed' ? redact(error) : undefined,
      }),
    );
    await writeJson(stateFile, state);
  }
  function broadcast(event) {
    for (const run of runs.values()) if (run.state === 'running') run.wire?.send(event);
  }
  function kill(run, signal) {
    try {
      process.kill(-run.process.pid, signal);
    } catch {
      try {
        run.process.kill(signal);
      } catch {}
    }
  }
  function fault(id, error) {
    const run = runs.get(id);
    if (!run || run.stopping) return;
    const entry = entries.get(id);
    entry.error = String(error.message);
    entry.state = 'failed';
    entry.enabled = false;
    recordLog(id, 'error', entry.error);
    run.reject(error);
    void stop(id).then(async () => {
      entry.state = 'failed';
      changed();
      if (entries.get(id) === entry) await enqueueMutation(save).catch(() => {});
    });
    for (const other of dependents(id).reverse())
      if (other !== id) {
        const candidate = entries.get(other);
        candidate.error = `Dependency unavailable: ${id}`;
        void stop(other).then(() => {
          candidate.state = 'blocked';
          changed();
        });
      }
  }
  async function startOne(id) {
    const entry = entries.get(id);
    if (!entry.trusted) throw new Error(`Trust must be explicitly granted to ${id}`);
    if (runs.get(id)?.state === 'running') return;
    if (runs.has(id)) return await runs.get(id).started;
    if (entry.manifest.nativeModules) nativeActivated.add(id);
    await fs.mkdir(dataDirectory(id), { recursive: true, mode: 0o700 });
    entry.error = '';
    entry.state = 'starting';
    const run = {
      id,
      state: 'starting',
      generation: ++generation,
      token: randomBytes(32).toString('hex'),
      registrations: Object.fromEntries(
        ['services', 'commands', 'music', 'lyrics', ...uiKinds].map((key) => [key, new Map()]),
      ),
    };
    run.started = new Promise((resolve, reject) => {
      run.resolve = resolve;
      run.reject = reject;
    });
    runs.set(id, run);
    run.process = spawn(process.execPath, [path.join(runtimeDirectory, 'worker.cjs')], {
      cwd: entry.directory,
      detached: process.platform !== 'win32',
      windowsHide: true,
      stdio: ['ignore', 'pipe', 'pipe'],
      env: {
        ...process.env,
        NEKOTUNE_EXTENSION_SOCKET: socketPath,
        NEKOTUNE_EXTENSION_TOKEN: run.token,
        NEKOTUNE_EXTENSION_ID: id,
      },
    });
    run.process.on('spawn', () =>
      host.send({ event: 'extensions.process', pid: run.process.pid, active: true }),
    );
    run.process.stdout.on('data', (bytes) => recordLog(id, 'info', bytes.toString()));
    run.process.stderr.on('data', (bytes) => recordLog(id, 'error', bytes.toString()));
    run.process.on('error', (error) => fault(id, error));
    run.process.on('exit', (code, signal) => {
      host.send({ event: 'extensions.process', pid: run.process.pid, active: false });
      if (!run.stopping && runs.get(id) === run) fault(id, new Error(`Extension exited (${signal ?? code})`));
    });
    run.startTimer = setTimeout(() => fault(id, new Error('Activation timed out')), 10_000);
    changed();
    try {
      await run.started;
      run.heartbeat = setInterval(() => {
        if (!run.pinging && !run.stopping) {
          run.pinging = true;
          run.wire
            .request('ping', {}, 15_000)
            .catch((error) => fault(id, error))
            .finally(() => {
              run.pinging = false;
            });
        }
      }, 5000);
      if (entry.development) {
        run.watcher = watch(entry.directory, { recursive: true }, (_event, name) => {
          if (!name || String(name).includes('node_modules') || String(name).endsWith('.tmp')) return;
          clearTimeout(run.debounce);
          run.debounce = setTimeout(
            () => enqueueMutation(() => reload(id)).catch((error) => recordLog(id, 'error', error.message)),
            350,
          );
        });
      }
    } catch (error) {
      await stop(id);
      entry.state = 'failed';
      entry.error = error.message;
      entry.enabled = false;
      changed();
      throw error;
    }
  }
  async function stop(id) {
    audioProxy.revoke(id);
    const run = runs.get(id);
    if (!run) return;
    if (run.stopping) return run.stopped;
    run.stopping = true;
    run.state = 'stopping';
    clearTimeout(run.startTimer);
    clearInterval(run.heartbeat);
    clearTimeout(run.debounce);
    run.watcher?.close();
    run.stopped = (async () => {
      await run.wire?.request('deactivate', {}, 3000).catch(() => {});
      run.wire?.socket.destroy();
      kill(run, 'SIGTERM');
      if (run.process.exitCode === null && run.process.signalCode === null)
        await new Promise((resolve) => {
          const timer = setTimeout(() => {
            kill(run, 'SIGKILL');
            resolve();
          }, 300);
          run.process.once('exit', () => {
            clearTimeout(timer);
            resolve();
          });
        });
      await run.storageWrites?.catch(() => {});
      await run.secretWrites?.catch(() => {});
      if (runs.get(id) === run) runs.delete(id);
      entries.get(id).state = 'disabled';
      changed();
    })();
    return run.stopped;
  }
  function dependents(id) {
    const found = new Set([id]);
    let size;
    do {
      size = found.size;
      for (const [candidate, entry] of entries)
        if (Object.keys(entry.manifest.dependencies ?? {}).some((dependency) => found.has(dependency)))
          found.add(candidate);
    } while (size !== found.size);
    return [...found];
  }
  async function enable(id, trusted = false) {
    if (process.env.NEKOTUNE_SAFE_MODE === '1')
      throw new Error('Restart without --safe-mode to enable extensions');
    const entry = entries.get(id);
    if (!entry) throw new Error('Extension not found');
    if (trusted) entry.trusted = true;
    if (!entry.trusted) throw new Error(`Trust must be explicitly granted to ${id}`);
    async function inspectGraph(candidate, visited = new Set()) {
      if (visited.has(candidate)) return;
      visited.add(candidate);
      const installed = entries.get(candidate);
      if (!installed) throw new Error(`Dependency missing: ${candidate}`);
      const manifest = await readManifest(installed.directory);
      if (manifest.id !== candidate) throw new Error('Manifest ID changed; reinstall the extension');
      installed.manifest = manifest;
      for (const dependency of Object.keys(manifest.dependencies ?? {}))
        await inspectGraph(dependency, visited);
    }
    await inspectGraph(id);
    const order = dependencyOrder(entries, id);
    for (const dependency of order)
      if (!entries.get(dependency).trusted)
        throw new Error(`Enable and trust dependency first: ${dependency}`);
    for (const dependency of order) {
      const candidate = entries.get(dependency);
      try {
        await startOne(dependency);
      } catch (error) {
        if (candidate.previous) {
          await stop(dependency);
          const restored = {
            ...candidate.previous,
            trusted: candidate.trusted,
            manifest: await readManifest(candidate.previous.directory),
            state: 'disabled',
          };
          entries.set(dependency, restored);
          await startOne(dependency);
          restored.enabled = true;
          await save();
          changed();
          throw new Error(`Activation rolled back: ${error.message}`);
        }
        await save();
        throw error;
      }
      candidate.enabled = true;
      delete candidate.previous;
    }
    await save();
    changed();
  }
  async function disable(id) {
    if (!entries.has(id)) throw new Error('Extension not found');
    for (const dependent of dependents(id).reverse()) {
      await stop(dependent);
      if (dependent !== id) {
        entries.get(dependent).state = 'blocked';
        entries.get(dependent).error = `Dependency disabled: ${id}`;
      }
    }
    entries.get(id).enabled = false;
    await save();
    changed();
  }
  async function reload(id) {
    const entry = entries.get(id);
    if (!entry) throw new Error('Extension not found');
    if (entry.manifest.nativeModules) throw new Error('Native QML modules require application restart');
    const affected = dependents(id);
    const manifest = await readManifest(entry.directory);
    if (manifest.id !== id) throw new Error('Reload cannot change extension ID');
    host.send({ event: 'extensions.reloading', ids: affected, active: true });
    try {
      for (const dependent of [...affected].reverse()) await stop(dependent);
      entry.manifest = manifest;
      for (const dependent of affected) if (entries.get(dependent).enabled) await enable(dependent);
    } finally {
      host.send({ event: 'extensions.reloading', ids: affected, active: false });
    }
  }
  async function install(params) {
    if (typeof params.path !== 'string') throw new Error('Local package path required');
    let directory,
      stage,
      updating = [];
    try {
      if (params.development) directory = await fs.realpath(params.path);
      else {
        stage = path.join(root, 'packages', `.staging-${randomUUID()}`);
        await unpack(params.path, stage);
        directory = stage;
      }
      const manifest = await readManifest(directory),
        old = entries.get(manifest.id);
      if (nativeActivated.has(manifest.id))
        throw new Error('Native QML modules were loaded; restart in safe mode before updating');
      if (old && !params.replace) throw new Error('Extension already installed; set replace to update');
      if (!params.development) {
        directory = path.join(root, 'packages', `${manifest.id}-${manifest.version}-${randomUUID()}`);
        await fs.rename(stage, directory);
        stage = undefined;
      }
      const affected = old ? dependents(manifest.id) : [];
      updating = affected;
      if (updating.length) host.send({ event: 'extensions.reloading', ids: updating, active: true });
      for (const id of [...affected].reverse()) await stop(id);
      const entry = {
        directory,
        development: Boolean(params.development),
        manifest,
        trusted: Boolean(old?.trusted),
        enabled: Boolean(old?.enabled),
        state: 'disabled',
        previous:
          old && !old.enabled
            ? (old.previous ?? {
                directory: old.directory,
                development: old.development,
                enabled: false,
                trusted: old.trusted,
              })
            : undefined,
      };
      entries.set(manifest.id, entry);
      try {
        if (entry.enabled) await enable(manifest.id);
        await save();
        for (const id of affected) if (id !== manifest.id && entries.get(id).enabled) await enable(id);
      } catch (error) {
        await stop(manifest.id);
        if (old) {
          entries.set(manifest.id, old);
          if (old.enabled) await enable(manifest.id);
          for (const id of affected) if (id !== manifest.id && entries.get(id).enabled) await enable(id);
        } else entries.delete(manifest.id);
        await save();
        throw new Error(`Update rolled back: ${error.message}`);
      }
      changed();
      return { extension: publicEntry(entry) };
    } finally {
      if (updating.length) host.send({ event: 'extensions.reloading', ids: updating, active: false });
      if (stage) await fs.rm(stage, { recursive: true, force: true });
    }
  }
  async function setConfig({ id, config }) {
    if (
      !entries.has(id) ||
      !config ||
      typeof config !== 'object' ||
      Array.isArray(config) ||
      Buffer.byteLength(JSON.stringify(config)) > 256 * 1024
    )
      throw new Error('Invalid extension configuration');
    await writeJson(path.join(dataDirectory(id), 'config.json'), config);
    runs.get(id)?.wire?.send({ event: 'extension.config_changed', config });
    return { config };
  }
  async function invoke({ id, service, params = {} }) {
    const run = runs.get(id);
    if (run?.state !== 'running') throw new Error('Extension is not running');
    if (typeof service !== 'string') throw new Error('Service name required');
    const result = await run.wire.request('service', {
      service: service.startsWith(id + '/') ? service.slice(id.length + 1) : service,
      params,
    });
    if (runs.get(id) !== run || run.state !== 'running')
      throw new Error('Extension generation is no longer active');
    if (!result || typeof result !== 'object' || Array.isArray(result))
      throw new Error('Service must return an object; wrap scalar results in {value}');
    return result;
  }
  function enqueueMutation(operation) {
    const next = mutations.then(operation);
    mutations = next.catch(() => {});
    return next;
  }
  async function dispatch(method, params) {
    if (method === 'extensions.resolve_audio') return await audioProxy.open(params.source, params.id);
    if (method === 'extensions.release_audio') {
      audioProxy.release(params.url);
      return {};
    }
    if (method === 'extensions.download_audio')
      return await audioProxy.download(params.source, params.id, params.base, params.task, (progress) =>
        host.send({ event: 'music.download', task: params.task, state: 'running', ...progress }),
      );
    if (method === 'extensions.cancel_download') {
      audioProxy.cancelDownload(params.task);
      return {};
    }
    if (method === 'extensions.download_completed')
      return dispatch('extensions.provider_call', {
        source: params.source,
        kind: 'music',
        operation: 'downloaded',
        params,
      });
    if (method === 'extensions.list') return snapshot();
    if (method === 'extensions.logs')
      return {
        logs: (logs.get(params.id) ?? []).map((entry) => ({ ...entry, message: redact(entry.message) })),
      };
    if (method === 'extensions.get_config') {
      const entry = entries.get(params.id);
      if (!entry) throw new Error('Extension not found');
      return {
        config: await readJson(
          path.join(dataDirectory(params.id), 'config.json'),
          entry.manifest.config ?? {},
        ),
      };
    }
    if (method === 'extensions.call') return await invoke(params);
    if (method === 'extensions.provider_call') {
      const slash = String(params.source).indexOf('/'),
        id = String(params.source).slice(0, slash),
        localId = String(params.source).slice(slash + 1);
      const run = runs.get(id);
      if (slash < 0 || run?.state !== 'running' || !run.registrations[params.kind]?.has(localId))
        throw new Error('Source is unavailable');
      const result = await run.wire.request(
        'provider',
        {
          kind: params.kind,
          id: localId,
          operation: params.operation,
          params: params.params ?? {},
        },
        params.operation === 'downloaded' ? 120_000 : 30_000,
      );
      if (runs.get(id) !== run || run.state !== 'running')
        throw new Error('Extension generation is no longer active');
      return result;
    }
    if (method === 'host.event') {
      broadcast(params);
      return {};
    }
    if (method === 'shutdown') {
      await shutdown();
      return {};
    }
    return await enqueueMutation(async () => {
      if (method === 'extensions.install') return await install(params);
      if (method === 'extensions.enable') {
        await enable(params.id, params.trusted === true);
        for (const [id, entry] of entries)
          if (entry.enabled && entry.state === 'blocked') await enable(id).catch(() => {});
      } else if (method === 'extensions.disable') await disable(params.id);
      else if (method === 'extensions.reload') await reload(params.id);
      else if (method === 'extensions.set_config') return await setConfig(params);
      else if (method === 'extensions.select') {
        if (typeof params.slot !== 'string' || typeof params.contribution !== 'string')
          throw new Error('Invalid selection');
        if (
          params.contribution &&
          !snapshot().extensions.some((entry) =>
            (entry.contributes[params.slot === 'theme' ? 'themes' : 'slots'] ?? []).some(
              (item) =>
                item.id === params.contribution && (params.slot === 'theme' || item.slot === params.slot),
            ),
          )
        )
          throw new Error('Contribution is unavailable for this slot');
        state.selections[params.slot] = params.contribution;
        await save();
        changed();
      } else if (method === 'extensions.uninstall') {
        const entry = entries.get(params.id);
        if (!entry) throw new Error('Extension not found');
        await disable(params.id);
        if (params.clearData === true)
          for (const key of await readJson(path.join(dataDirectory(params.id), 'secret-keys.json'), []))
            await host.request('secret', { operation: 'delete', key, extensionId: params.id });
        entries.delete(params.id);
        await save();
        if (!entry.development && entry.directory.startsWith(path.join(root, 'packages') + path.sep))
          await fs.rm(entry.directory, { recursive: true, force: true });
        if (params.clearData === true)
          await fs.rm(dataDirectory(params.id), { recursive: true, force: true });
        changed();
      } else throw new Error(`Unknown extension operation: ${method}`);
      return snapshot();
    });
  }
  async function shutdown() {
    if (closing) return;
    closing = true;
    audioProxy.close();
    await Promise.allSettled([...runs.keys()].map(stop));
    server.close();
    await fs.rm(socketDirectory, { recursive: true, force: true });
  }
  host.on('message', (message) => {
    void host.handle(message, dispatch);
  });
  process.on('SIGTERM', () => {
    void shutdown().finally(() => process.exit(0));
  });
  await new Promise((resolve, reject) => {
    if (host.socket.readyState === 'open') resolve();
    else {
      host.socket.once('connect', resolve);
      host.socket.once('error', reject);
    }
  });
  await host.request('hello', { token: process.env.NEKOTUNE_EXTENSION_TOKEN });
  try {
    state = await readJson(stateFile, state);
    for (const stored of state.entries ?? []) {
      try {
        const manifest = await readManifest(stored.directory);
        if (manifest.id !== stored.id) throw new Error('Installed extension ID changed');
        entries.set(manifest.id, {
          ...stored,
          manifest,
          state: stored.failure ? 'failed' : 'disabled',
          error: stored.failure ?? '',
        });
      } catch (error) {
        entries.set(stored.id, {
          ...stored,
          manifest: { id: stored.id, name: stored.id, version: '0.0.0' },
          state: 'failed',
          error: error.message,
        });
      }
    }
    const bundledDirectory = process.env.NEKOTUNE_BUNDLED_EXTENSIONS;
    if (bundledDirectory) {
      state.bundledSeen ??= [];
      for (const name of await fs.readdir(bundledDirectory).catch(() => [])) {
        if (!name.endsWith('.zip') || state.bundledSeen.includes(name)) continue;
        const id = name.slice(0, -4);
        try {
          if (!entries.has(id)) await install({ path: path.join(bundledDirectory, name) });
          state.bundledSeen.push(name);
          await save();
        } catch (error) {
          host.send({ event: 'extensions.error', message: `Bundled extension ${id}: ${error.message}` });
        }
      }
    }
    catalogueReady = true;
    changed();
    if (process.env.NEKOTUNE_SAFE_MODE !== '1')
      for (const [id, entry] of entries)
        if (entry.enabled && entry.state !== 'failed')
          await (async () => {
            const order = dependencyOrder(entries, id);
            if (
              order.some(
                (dependency) =>
                  !entries.get(dependency).enabled || entries.get(dependency).state === 'failed',
              )
            )
              throw new Error('Dependency is disabled or failed; enable it manually');
            await enable(id);
          })().catch((error) => {
            if (entry.state !== 'failed') entry.state = 'blocked';
            entry.error = error.message;
            changed();
          });
  } catch (error) {
    host.send({ event: 'extensions.error', message: error.message });
  }
}
main().catch((error) => {
  console.error(error);
  process.exit(1);
});

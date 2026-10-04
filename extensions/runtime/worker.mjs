import net from 'node:net';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { Wire } from './wire.mjs';

const socket = net.createConnection(process.env.NEKOTUNE_EXTENSION_SOCKET);
const wire = new Wire(socket);
const services = new Map(),
  commands = new Map(),
  music = new Map(),
  lyrics = new Map(),
  subscriptions = new Map();
const disposables = [];
let module,
  context,
  stopping = false;
wire.on('fault', (error) => console.error(error.message));
wire.on('close', () => process.exit(stopping ? 0 : 1));
const register =
  (kind, registry) =>
  async (id, implementation, descriptor = {}) => {
    if (registry.has(id)) throw new Error(`Duplicate ${kind}: ${id}`);
    registry.set(id, implementation);
    try {
      await wire.request('register', { kind, id, descriptor });
    } catch (error) {
      registry.delete(id);
      throw error;
    }
    const dispose = async () => {
      registry.delete(id);
      await wire.request('unregister', { kind, id });
    };
    disposables.push(dispose);
    return dispose;
  };
wire.on('message', (message) => {
  if (message.event) {
    if (message.event === 'extension.config_changed') context.config = message.config;
    for (const [event, handlers] of subscriptions)
      if (event === '*' || event === message.event)
        for (const handler of handlers)
          Promise.resolve()
            .then(() => handler(message))
            .catch((error) => console.error(error));
    return;
  }
  void wire.handle(message, async (method, params) => {
    if (method === 'ping') return {};
    if (method === 'deactivate') {
      stopping = true;
      try {
        if (module?.deactivate) await module.deactivate(context);
      } catch (error) {
        console.error(error);
      }
      for (const dispose of disposables.reverse()) {
        try {
          await dispose();
        } catch (error) {
          console.error(error);
        }
      }
      setImmediate(() => socket.end());
      return {};
    }
    if (method === 'service') {
      const handler = services.get(params.service) ?? commands.get(params.service);
      if (!handler) throw new Error(`Unknown service/command: ${params.service}`);
      return await handler(params.params ?? {});
    }
    if (method === 'provider') {
      const provider = (params.kind === 'music' ? music : lyrics).get(params.id);
      if (params.operation === 'downloaded' && provider && typeof provider.downloaded !== 'function')
        return {};
      if (
        params.kind === 'music' &&
        params.operation === 'download' &&
        provider &&
        typeof provider.download !== 'function'
      )
        return await provider.resolve({ ...params.params, purpose: 'download' });
      if (!provider || typeof provider[params.operation] !== 'function')
        throw new Error(`Unsupported provider operation: ${params.operation}`);
      return await provider[params.operation](params.params ?? {});
    }
    throw new Error(`Unknown worker method: ${method}`);
  });
});
socket.on('connect', async () => {
  try {
    const boot = await wire.request('hello', {
      token: process.env.NEKOTUNE_EXTENSION_TOKEN,
      id: process.env.NEKOTUNE_EXTENSION_ID,
    });
    const id = boot.manifest.id;
    context = {
      id,
      version: boot.manifest.version,
      directory: boot.directory,
      dataDirectory: boot.dataDirectory,
      config: boot.config,
      host: { call: (method, params = {}) => wire.request('host.call', { method, params }) },
      events: {
        on(event, handler) {
          if (!subscriptions.has(event)) subscriptions.set(event, new Set());
          subscriptions.get(event).add(handler);
          const dispose = () => subscriptions.get(event)?.delete(handler);
          disposables.push(dispose);
          return dispose;
        },
        emit: (event, data = {}) => wire.request('event', { event, data }),
      },
      services: {
        register: register('services', services),
        call: (extension, service, params = {}) =>
          wire.request('host.call', {
            method: 'extensions.call',
            params: { id: extension, service, params },
          }),
      },
      commands: { register: register('commands', commands) },
      ui: {
        async register(kind, id, descriptor) {
          await wire.request('register', { kind, id, descriptor });
          const dispose = () => wire.request('unregister', { kind, id });
          disposables.push(dispose);
          return dispose;
        },
      },
      music: { register: register('music', music) },
      lyrics: { register: register('lyrics', lyrics) },
      storage: {
        get: (key) => wire.request('storage.get', { key }).then((result) => result.value),
        set: (key, value) => wire.request('storage.set', { key, value }),
      },
      secrets: {
        get: (key) => wire.request('secret', { operation: 'get', key }).then((result) => result.value),
        set: (key, value) => wire.request('secret', { operation: 'set', key, value }),
        delete: (key) => wire.request('secret', { operation: 'delete', key }),
      },
      settings: { update: (config) => wire.request('config', { config }) },
      tasks: {
        report: (task, progress) => wire.request('event', { event: 'task', data: { task, ...progress } }),
      },
      subscriptions: disposables,
      log: console,
    };
    if (boot.manifest.main) {
      module = await import(pathToFileURL(path.join(boot.directory, boot.manifest.main)).href);
      if (typeof module.activate !== 'function') throw new Error('Entry must export activate(context)');
      const disposable = await module.activate(context);
      if (typeof disposable === 'function') disposables.push(disposable);
    }
    await wire.request('ready');
  } catch (error) {
    console.error(error.stack || error.message);
    await wire.request('activation.failed', { message: error.message }).catch(() => {});
    process.exit(1);
  }
});

import fs from 'node:fs/promises';
import path from 'node:path';
import { KugouClient, businessOk, workerOrigin, validHash } from './client.mjs';
import { lyricCandidates, lyricDocument, saveAssets } from './lyrics.mjs';

export async function activate(context) {
  if (!context.config.legacy_imported) {
    const host = await context.host.call('player.status');
    let legacy = {};
    try {
      legacy =
        JSON.parse(await fs.readFile(path.join(host.config_directory, 'settings.json'), 'utf8')).kugou || {};
    } catch (error) {
      if (error.code !== 'ENOENT')
        context.log.warn('Legacy configuration unavailable; configure Kugou in plugin settings');
    }
    let url = '';
    try {
      url = workerOrigin(legacy.worker_url, false);
    } catch {}
    await context.settings.update({
      enabled: Boolean(legacy.enabled && url),
      worker_url: url,
      ...context.config,
      legacy_imported: true,
    });
  }
  const client = new KugouClient(context);
  await client.initialize();
  let busy = false,
    activeTask = '',
    closed = false,
    sourceConfig,
    sourceUpdates = Promise.resolve();
  const tracks = new Map(Object.entries((await context.storage.get('tracks')) || {})),
    registrations = [];
  const emit = (name, data = {}) => context.events.emit(name, data);
  const status = () => ({ ...client.status(), busy, download_active: Boolean(activeTask) });
  const notify = () => emit('status', status());
  context.subscriptions.push(() => {
    closed = true;
    client.cancel();
  });
  async function remember(result) {
    for (const track of result.tracks) {
      tracks.set(track.id, track);
    }
    while (tracks.size > 1000) tracks.delete(tracks.keys().next().value);
    await context.storage.set('tracks', Object.fromEntries(tracks));
    return result;
  }
  async function track(id) {
    if (!validHash(id)) throw new Error('Invalid Kugou track ID');
    return tracks.get(id) || { id, title: id };
  }
  const provider = {
    search: async ({ query, cursor }) => remember(await client.search(query, cursor ? Number(cursor) : 1)),
    track: async ({ id }) => track(id),
    resolve: async ({ id }) => client.resolve(await track(id)),
    downloaded: async ({ id, path: audioPath, existing }) =>
      saveAssets(client, await track(id), audioPath, existing),
  };
  async function updateSources() {
    if (closed) return;
    const signature = JSON.stringify([context.config.enabled, context.config.worker_url]);
    if (signature === sourceConfig) return;
    for (const unregister of registrations.splice(0)) await unregister();
    sourceConfig = undefined;
    if (!client.configured()) {
      sourceConfig = signature;
      return;
    }
    workerOrigin(context.config.worker_url);
    registrations.push(
      await context.music.register('music', provider, {
        name: '酷狗 / Kugou',
        download: true,
        page: 'music',
      }),
    );
    registrations.push(
      await context.lyrics.register(
        'lyrics',
        {
          search: async (query) => ({
            candidates: (await remember(await client.search(query.title))).tracks.map((item) => ({
              ...item,
              song_result: true,
              score: 0,
            })),
          }),
          resolve: async (candidate) => {
            if (candidate.song_result) return { candidates: await lyricCandidates(client, candidate) };
            const { binary, ...document } = await lyricDocument(client, candidate);
            return document;
          },
        },
        { name: '酷狗 / Kugou' },
      ),
    );
    sourceConfig = signature;
  }
  function configureSources() {
    // Settings events and service calls can arrive together; only one may change registrations at a time.
    const update = sourceUpdates.then(updateSources);
    sourceUpdates = update.catch(() => {});
    return update;
  }
  async function operation(name, action) {
    if (busy) throw new Error('Kugou operation already in progress');
    busy = true;
    await notify();
    void (async () => {
      try {
        const result = await action();
        if (!closed) await emit(name, result || {});
      } catch (error) {
        if (!closed) await emit('operation_failed', { message: error.message });
      } finally {
        busy = false;
        if (!closed) await notify();
      }
    })();
    return { started: true };
  }
  const services = {
    status: async () => status(),
    'config.set': async ({ enabled, worker_url }) => {
      if (typeof enabled !== 'boolean' || typeof worker_url !== 'string')
        throw new Error('Invalid Kugou configuration');
      if (busy || activeTask) throw new Error('Kugou operation already in progress');
      const config = { ...context.config, enabled, worker_url: workerOrigin(worker_url, enabled) };
      await context.settings.update(config);
      await configureSources();
      await emit('config_changed');
      await notify();
      return status();
    },
    save_key: async ({ key }) => {
      if (busy) throw new Error('Kugou operation already in progress');
      await client.saveKey(key);
      await notify();
      return status();
    },
    clear_key: async () => {
      if (busy) throw new Error('Kugou operation already in progress');
      await client.clearKey();
      await notify();
      return status();
    },
    send_code: async ({ mobile }) => {
      if (!/^1[3-9]\d{9}$/.test(mobile || '')) throw new Error('Invalid mainland China mobile number');
      return operation('code_sent', async () => {
        if (!client.cookies.dfid || ['0', '-', 'undefined'].includes(client.cookies.dfid)) {
          const registered = await client.account('/register/dev', {});
          if (!businessOk(registered) || !client.cookies.dfid)
            throw new Error('Kugou device registration failed');
        }
        if (!businessOk(await client.account('/captcha/sent', { mobile })))
          throw new Error('Kugou rejected SMS request');
      });
    },
    login: async ({ mobile, code }) => {
      if (!/^1[3-9]\d{9}$/.test(mobile || '') || !/^\d{4,8}$/.test(code || ''))
        throw new Error('Invalid mobile number or verification code');
      return operation('logged_in', async () => {
        const result = await client.account('/login/cellphone', { mobile, code });
        if (
          !businessOk(result) ||
          !client.loggedIn() ||
          client.cookies.token !== result.data?.token ||
          client.cookies.userid !== String(result.data?.userid)
        )
          throw new Error('Kugou login failed or needs extra verification');
      });
    },
    search: async ({ keywords, page = 1 }) =>
      operation('search_results', async () => ({
        songs: (await remember(await client.search(keywords, page))).tracks.map((item) => ({
          ...item,
          hash: item.id,
        })),
        page,
      })),
    download: async ({ hash }) => {
      if (activeTask) throw new Error('A Kugou download is already in progress');
      const item = await track(hash);
      const result = await context.host.call('music.download', {
        source: context.id + '/music',
        track: item,
      });
      activeTask = result.task;
      await notify();
      return result;
    },
    cancel: async () => {
      if (!activeTask) throw new Error('No active Kugou download');
      return context.host.call('music.cancel', { task: activeTask });
    },
    play: async ({ hash }) =>
      context.host.call('music.enqueue', {
        source: context.id + '/music',
        track: await track(hash),
        play: true,
      }),
  };
  for (const [name, handler] of Object.entries(services)) await context.services.register(name, handler);
  context.events.on('extension.config_changed', async () => {
    try {
      await configureSources();
    } catch (error) {
      await emit('operation_failed', { message: error.message });
    }
    await emit('config_changed');
    await notify();
  });
  context.events.on('music.download', async (event) => {
    if (event.task !== activeTask) return;
    if (event.state === 'running') await emit('download_progress', event);
    else {
      activeTask = '';
      await emit(
        event.state === 'finished'
          ? 'download_finished'
          : event.state === 'cancelled'
            ? 'download_cancelled'
            : 'operation_failed',
        event,
      );
      await notify();
    }
  });
  await configureSources();
}

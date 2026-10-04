import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { deflateSync } from 'node:zlib';
import { activate } from '../builtin/kugou/main.mjs';
import {
  KugouClient,
  workerOrigin,
  parseTracks,
  audioUrl,
  coverUrl,
  decodeKrc,
} from '../builtin/kugou/client.mjs';
import { lyricCandidates, lyricDocument, chooseAutomatic, saveAssets } from '../builtin/kugou/lyrics.mjs';

const hash = '0123456789abcdef0123456789abcdef';
function fixture() {
  const secrets = new Map([['account-key', 'fixture-key']]),
    storage = new Map(),
    services = new Map(),
    music = new Map(),
    lyrics = new Map(),
    events = [],
    listeners = new Map();
  const register = (map) => async (id, implementation) => {
    assert.ok(!map.has(id), `Duplicate registration: ${id}`);
    map.set(id, implementation);
    return async () => map.delete(id);
  };
  const context = {
    id: 'nekotune.kugou',
    config: { enabled: true, worker_url: 'https://worker.example', legacy_imported: true },
    secrets: {
      get: async (key) => secrets.get(key) ?? null,
      set: async (key, value) => secrets.set(key, value),
      delete: async (key) => secrets.delete(key),
    },
    storage: {
      get: async (key) => storage.get(key) ?? null,
      set: async (key, value) => storage.set(key, value),
    },
    settings: {
      update: async (config) => {
        context.config = config;
        await listeners.get('extension.config_changed')?.({ config });
      },
    },
    subscriptions: [],
    log: console,
    services: { register: register(services) },
    music: { register: register(music) },
    lyrics: { register: register(lyrics) },
    events: {
      emit: async (event, data) => events.push({ event, data }),
      on: (event, fn) => listeners.set(event, fn),
    },
    host: { call: async (method, params) => ({ method, params, task: 'fixture-task' }) },
  };
  return { context, secrets, storage, services, music, lyrics, events, listeners };
}
function json(body, status = 200) {
  return new Response(JSON.stringify(body), { status, headers: { 'Content-Type': 'application/json' } });
}
function encodedKrc(text = '[0,2000]<0,1000,0>Hello<1000,1000,0>world') {
  const compressed = deflateSync(text),
    key = [0x40, 0x47, 0x61, 0x77, 0x5e, 0x32, 0x74, 0x47, 0x51, 0x36, 0x31, 0x2d, 0xce, 0xd2, 0x6e, 0x69];
  for (let i = 0; i < compressed.length; i++) compressed[i] ^= key[i % 16];
  return { status: 200, content: Buffer.concat([Buffer.from('krc1'), compressed]).toString('base64') };
}
test('Kugou routes, grouped search, canonical hashes and URL boundaries', () => {
  assert.equal(
    workerOrigin('https://kugou-lyrics-api.lyuy.workers.dev/'),
    'https://luy-music-api.lyuy.workers.dev',
  );
  assert.equal(workerOrigin('http://127.0.0.1:1234'), 'http://127.0.0.1:1234');
  for (const url of [
    'http://public.example',
    'https://worker.example/api/music/kugou/v1',
    'https://key@worker.example',
    'https://worker.example?key=secret',
  ])
    assert.throws(() => workerOrigin(url));
  assert.throws(() => audioUrl('https://kugou.com.evil.example/song'));
  assert.throws(() => audioUrl('http://fs.kugou.com/song'));
  assert.equal(coverUrl('http://imge.kugou.com/{size}/cover.png'), 'https://imge.kugou.com/240/cover.png');
  const tracks = parseTracks({
    status: 1,
    error_code: 0,
    data: {
      lists: [
        {
          FileHash: hash.toUpperCase(),
          SongName: '<em>Title</em>',
          SingerName: 'Singer',
          Duration: 3,
          Image: 'http://imge.kugou.com/{size}/a.png',
          Grp: [{ FileHash: hash }, { FileHash: 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa', SongName: 'Variant' }],
        },
      ],
    },
  });
  assert.equal(tracks.length, 2);
  assert.equal(tracks[0].id, hash);
  assert.equal(tracks[0].duration_ms, 3000);
  assert.equal(tracks[1].artist, 'Singer');
  assert.equal(tracks[0].title, 'Title');
});
test('Kugou account state is persisted before success and never appears in public status', async () => {
  const { context, secrets } = fixture();
  const requests = [];
  const client = new KugouClient(context, async (url, options) => {
    requests.push({ url: String(url), options });
    return json({ status: 1, cookies: { token: 'private-token', userid: '7', dfid: 'device' } });
  });
  await client.initialize();
  await client.account('/login/cellphone', { mobile: '13800138000', code: '123456' });
  assert.equal(requests[0].url, 'https://worker.example/api/music/kugou/v1/login/cellphone');
  assert.equal(requests[0].options.headers['X-Account-Key'], 'fixture-key');
  assert.equal(client.loggedIn(), true);
  assert.equal(JSON.parse(secrets.get('account-session')).cookies.token, 'private-token');
  assert.ok(!JSON.stringify(client.status()).includes('private-token'));
  assert.ok(!JSON.stringify(client.status()).includes('fixture-key'));
  const previous = { ...client.cookies };
  context.secrets.set = async () => {
    throw new Error('Keyring unavailable');
  };
  await assert.rejects(client.account('/song/url', {}), /Keyring unavailable/);
  assert.deepEqual(client.cookies, previous);
  await assert.rejects(client.saveKey('new-key'));
  assert.equal(client.key, 'fixture-key');
});
test('Kugou refuses redirects, oversized replies and missing playback entitlement', async () => {
  const { context } = fixture();
  const redirected = new KugouClient(
    context,
    async () => new Response('', { status: 302, headers: { location: 'https://evil.example' } }),
  );
  await redirected.initialize();
  await assert.rejects(redirected.search('Song'), /HTTP 302/);
  const oversized = new KugouClient(context, async () => new Response('x'.repeat(4 * 1024 * 1024 + 1)));
  await oversized.initialize();
  await assert.rejects(oversized.search('Song'), /too large/);
  const denied = new KugouClient(context, async () => json({ status: 0, fail_process: ['buy'] }));
  await denied.initialize();
  denied.cookies = { token: 'token', userid: '1', dfid: 'device' };
  await assert.rejects(denied.resolve({ id: hash }), /no playback permission/);
});
test('Kugou staged lyrics preserve the chosen version, decode KRC and fall back to LRC', async () => {
  const { context } = fixture();
  let brokenKrc = false;
  const calls = [];
  const client = new KugouClient(context, async (url) => {
    url = new URL(url);
    calls.push(url);
    if (url.pathname.endsWith('/search/lyric'))
      return json({
        status: 200,
        candidates: [{ id: 12, accesskey: 'lyric-key', song: 'Title', singer: 'Singer', duration: 3000 }],
      });
    if (url.hostname === 'lyrics.kugou.com') return json(brokenKrc ? { status: 500 } : encodedKrc());
    return json({ status: 200, decodeContent: '[00:01.00]Fallback' });
  });
  const track = { id: hash, title: 'Title', artist: 'Singer', duration_ms: 3000 };
  const candidates = await lyricCandidates(client, track);
  assert.equal(candidates.length, 1);
  assert.equal(chooseAutomatic(candidates, track).id, '12');
  assert.equal(chooseAutomatic([...candidates, ...candidates], track), null);
  const krc = await lyricDocument(client, candidates[0]);
  assert.ok(krc.krc_lyrics.includes('Hello'));
  assert.equal(krc.binary.subarray(0, 4).toString(), 'krc1');
  assert.equal(calls[1].searchParams.get('fmt'), 'krc');
  assert.equal(calls[1].hostname, 'lyrics.kugou.com');
  brokenKrc = true;
  assert.equal((await lyricDocument(client, candidates[0])).synced_lyrics, '[00:01.00]Fallback');
  assert.throws(() => decodeKrc({ status: 200, content: '%%%' }));
  assert.throws(() => decodeKrc(encodedKrc('not lyrics')));
});
test('Kugou downloaded sidecars preserve existing user assets and skip duplicate-song enrichment', async (t) => {
  const folder = await fs.mkdtemp(path.join(os.tmpdir(), 'kugou-assets-'));
  t.after(() => fs.rm(folder, { recursive: true, force: true }));
  await fs.writeFile(path.join(folder, 'track.lrc'), 'user lyrics');
  const { context } = fixture();
  let requests = 0;
  const client = new KugouClient(context, async () => {
    requests++;
    throw new Error('Should not fetch');
  });
  const statuses = await saveAssets(client, { id: hash }, path.join(folder, 'track.mp3'), false);
  assert.equal(statuses.lyric_status, 'existing');
  assert.equal(requests, 0);
  await saveAssets(
    client,
    { id: hash, cover_url: 'https://imge.kugou.com/a.png' },
    path.join(folder, 'other.mp3'),
    true,
  );
  assert.equal(requests, 0);
  assert.equal(await fs.readFile(path.join(folder, 'track.lrc'), 'utf8'), 'user lyrics');
});
test('Kugou extension dynamically registers sources, protects configuration and calls generic download APIs', async () => {
  const f = fixture();
  await activate(f.context);
  assert.ok(f.music.has('music'));
  assert.ok(f.lyrics.has('lyrics'));
  assert.equal((await f.services.get('status')()).key_saved, true);
  await assert.rejects(f.services.get('config.set')({ enabled: true, worker_url: 'http://evil.example' }));
  assert.equal(f.context.config.worker_url, 'https://worker.example');
  const result = await f.services.get('download')({ hash });
  assert.equal(result.method, 'music.download');
  assert.equal(result.params.source, 'nekotune.kugou/music');
  await f.listeners.get('music.download')({ task: 'fixture-task', state: 'finished', song_id: 7 });
  assert.ok(f.events.some((event) => event.event === 'download_finished' && event.data.song_id === 7));
  await f.services.get('config.set')({ enabled: false, worker_url: 'https://worker.example' });
  assert.equal(f.music.size, 0);
  assert.equal(f.lyrics.size, 0);
  // The manager writes config directly instead of invoking the plugin's config.set service.
  await f.context.settings.update({ ...f.context.config, enabled: true });
  assert.ok(f.music.has('music'));
  assert.ok(f.lyrics.has('lyrics'));
  await Promise.all([
    f.context.settings.update({ ...f.context.config, worker_url: 'https://other-worker.example' }),
    f.context.settings.update({ ...f.context.config, enabled: false }),
    f.context.settings.update({ ...f.context.config, enabled: true }),
  ]);
  assert.equal(f.music.size, 1);
  assert.equal(f.lyrics.size, 1);
  await f.context.settings.update({ ...f.context.config, worker_url: 'http://evil.example' });
  assert.equal(f.music.size, 0);
  assert.equal(f.lyrics.size, 0);
  assert.ok(f.events.some((event) => event.event === 'operation_failed'));
  await f.context.settings.update({ ...f.context.config, worker_url: 'https://worker.example' });
  assert.ok(f.music.has('music'));
  assert.ok(f.lyrics.has('lyrics'));
  for (const dispose of f.context.subscriptions) await dispose();
});

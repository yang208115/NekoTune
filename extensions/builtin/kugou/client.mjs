import fs from 'node:fs/promises';
import { inflateSync } from 'node:zlib';

const prefix = '/api/music/kugou/v1';
const hashPattern = /^[a-f\d]{32}$/i;
const clean = (value) =>
  String(value ?? '')
    .replace(/<[^>]*>/g, '')
    .trim()
    .slice(0, 256);
export const validHash = (id) => typeof id === 'string' && hashPattern.test(id);
export function workerOrigin(value, enabled = true) {
  if (!String(value ?? '').trim() && !enabled) return '';
  let url;
  try {
    url = new URL(String(value).trim());
  } catch {
    throw new Error('Set a valid Kugou Worker HTTPS origin');
  }
  if (url.hostname === 'kugou-lyrics-api.lyuy.workers.dev') url.hostname = 'luy-music-api.lyuy.workers.dev';
  if (
    (url.protocol !== 'https:' &&
      !(url.protocol === 'http:' && ['127.0.0.1', 'localhost', '[::1]'].includes(url.hostname))) ||
    url.username ||
    url.password ||
    url.search ||
    url.hash ||
    url.pathname !== '/'
  )
    throw new Error('Invalid Worker URL: use an HTTPS origin without a path');
  return url.origin;
}
export function audioUrl(value) {
  let url;
  try {
    url = new URL(value);
  } catch {
    throw new Error('Kugou did not provide an audio URL');
  }
  if (
    url.protocol !== 'https:' ||
    !url.hostname.endsWith('.kugou.com') ||
    url.username ||
    url.password ||
    (url.port && url.port !== '443')
  )
    throw new Error('Kugou did not provide a trusted audio URL');
  return url.href;
}
export function coverUrl(value) {
  try {
    const url = new URL(String(value ?? '').replaceAll('{size}', '240'));
    if (
      !['http:', 'https:'].includes(url.protocol) ||
      url.hostname !== 'imge.kugou.com' ||
      url.username ||
      url.password ||
      (url.port && !['80', '443'].includes(url.port))
    )
      return '';
    url.protocol = 'https:';
    url.port = '';
    url.hash = '';
    return url.href;
  } catch {
    return '';
  }
}
export function businessOk(body, expected = ['1']) {
  return (
    expected.includes(String(body.status)) &&
    ['error_code', 'errcode'].every((key) => body[key] === undefined || String(body[key]) === '0')
  );
}
export function parseTracks(body) {
  if (!businessOk(body) || !Array.isArray(body.data?.lists)) throw new Error('Invalid Kugou search response');
  const seen = new Set(),
    tracks = [];
  for (const parent of body.data.lists)
    for (const row of [parent, ...(Array.isArray(parent.Grp) ? parent.Grp : [])]) {
      const id = String(row.FileHash ?? row.hash ?? '').toLowerCase();
      if (!validHash(id) || seen.has(id)) continue;
      seen.add(id);
      const audioId = String(row.MixSongID ?? parent.MixSongID ?? '');
      tracks.push({
        id,
        title: clean(row.SongName ?? parent.SongName) || id,
        artist: clean(row.SingerName ?? parent.SingerName),
        album: clean(row.AlbumName ?? parent.AlbumName),
        duration_ms: Math.max(0, Math.round(Number(row.Duration ?? parent.Duration) * 1000) || 0),
        cover_url: coverUrl(row.Image || parent.Image),
        audio_id: /^\d{1,15}$/.test(audioId) ? audioId : '',
      });
      if (tracks.length >= 100) return tracks;
    }
  return tracks;
}
export function decodeKrc(body) {
  if (
    Number(body.status) !== 200 ||
    typeof body.content !== 'string' ||
    body.content.length > 4 * 1024 * 1024 ||
    !/^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/.test(body.content)
  )
    throw new Error('Invalid KRC response');
  const binary = Buffer.from(body.content, 'base64');
  if (binary.subarray(0, 4).toString() !== 'krc1') throw new Error('Invalid KRC header');
  const key = [
    0x40, 0x47, 0x61, 0x77, 0x5e, 0x32, 0x74, 0x47, 0x51, 0x36, 0x31, 0x2d, 0xce, 0xd2, 0x6e, 0x69,
  ];
  const compressed = Buffer.from(binary.subarray(4));
  for (let i = 0; i < compressed.length; i++) compressed[i] ^= key[i % 16];
  const result = inflateSync(compressed, { maxOutputLength: 16 * 1024 * 1024, info: true });
  if (result.engine.bytesWritten !== compressed.length) throw new Error('Trailing KRC data');
  const text = new TextDecoder('utf-8', { fatal: true }).decode(result.buffer);
  if (!/^\[\d+,\d+\].*<\d+,\d+,\d+>/m.test(text)) throw new Error('KRC contains no timed words');
  return { binary, text };
}
export async function externalKey(env = process.env) {
  const value = env.KUGOU_ACCOUNT_API_KEY?.trim();
  if (value) return validateKey(value);
  if (!env.KUGOU_ACCOUNT_API_KEY_FILE) return '';
  const stat = await fs.stat(env.KUGOU_ACCOUNT_API_KEY_FILE);
  if (stat.size > 4096) throw new Error('External key file is too large');
  return validateKey(await fs.readFile(env.KUGOU_ACCOUNT_API_KEY_FILE, 'utf8'));
}
export function validateKey(value) {
  if (
    typeof value !== 'string' ||
    !value.trim() ||
    Buffer.byteLength(value) > 4096 ||
    /[\x00-\x1f\x7f]/.test(value.trim())
  )
    throw new Error('Kugou account key must be 1 to 4096 printable bytes');
  return value.trim();
}
export class KugouClient {
  constructor(context, fetcher = fetch) {
    this.context = context;
    this.fetcher = fetcher;
    this.requests = new Set();
    this.key = '';
    this.keySaved = false;
    this.cookies = {};
    this.credentialError = '';
    this.sessionWrites = Promise.resolve();
  }
  async initialize() {
    try {
      const saved = await this.context.secrets.get('account-key');
      this.keySaved = Boolean(saved);
      this.key = saved || (await externalKey());
    } catch {
      this.credentialError = 'System keyring unavailable or legacy credential migration failed';
    }
    try {
      const saved = await this.context.secrets.get('account-session');
      if (saved) {
        const session = JSON.parse(saved);
        if (
          session.version !== 1 ||
          !session.cookies ||
          Object.values(session.cookies).some((value) => typeof value !== 'string')
        )
          throw new Error();
        this.cookies = session.cookies;
      }
    } catch {
      this.credentialError = 'System keyring unavailable or legacy session migration failed';
    }
  }
  configured() {
    return Boolean(this.context.config.enabled && this.context.config.worker_url);
  }
  loggedIn() {
    return Boolean(
      this.cookies.token &&
        this.cookies.userid &&
        this.cookies.dfid &&
        !['0', '-', 'undefined'].includes(this.cookies.dfid),
    );
  }
  status() {
    return {
      enabled: Boolean(this.context.config.enabled),
      worker_url: this.context.config.worker_url || '',
      configured: this.configured() && Boolean(this.key),
      key_saved: this.keySaved,
      logged_in: this.configured() && this.loggedIn(),
      credential_error: this.credentialError,
    };
  }
  async saveKey(value) {
    const next = validateKey(value);
    await this.context.secrets.set('account-key', next);
    if ((await this.context.secrets.get('account-key')) !== next)
      throw new Error('Cannot verify system keyring write');
    this.key = next;
    this.keySaved = true;
    this.credentialError = '';
  }
  async clearKey() {
    await this.context.secrets.delete('account-key');
    this.key = await externalKey();
    this.keySaved = false;
  }
  cancel() {
    for (const request of this.requests) request.abort();
  }
  async bytes(url, { body, limit = 4 * 1024 * 1024, accept = 'application/json' } = {}) {
    const cancellation = new AbortController();
    this.requests.add(cancellation);
    try {
      const response = await this.fetcher(url, {
        method: body ? 'POST' : 'GET',
        redirect: 'manual',
        signal: AbortSignal.any([cancellation.signal, AbortSignal.timeout(20_000)]),
        headers: {
          Accept: accept,
          'User-Agent': 'NekoTune-Kugou/1.0',
          ...(body ? { 'Content-Type': 'application/json', 'X-Account-Key': this.key } : {}),
        },
        ...(body ? { body: JSON.stringify(body) } : {}),
      });
      if (!response.ok || !response.body) throw new Error(`Kugou request failed: HTTP ${response.status}`);
      let size = 0;
      const chunks = [];
      for await (const chunk of response.body) {
        size += chunk.length;
        if (size > limit) throw new Error('Kugou response is too large');
        chunks.push(chunk);
      }
      return { bytes: Buffer.concat(chunks), type: response.headers.get('content-type') || '' };
    } catch (error) {
      if (cancellation.signal.aborted) throw new Error('Cancelled');
      if (/^Kugou (request failed: HTTP \d+|response is too large)$/.test(error.message)) throw error;
      throw new Error('Cannot reach Kugou service or request timed out');
    } finally {
      cancellation.abort();
      this.requests.delete(cancellation);
    }
  }
  async json(route, query = {}, body) {
    if (!this.configured()) throw new Error('Enable Kugou and configure its Worker in plugin settings');
    const url = new URL(
      route.startsWith('https:') ? route : workerOrigin(this.context.config.worker_url) + prefix + route,
    );
    url.search = new URLSearchParams(
      Object.entries(query).filter(([, value]) => value !== undefined && value !== ''),
    );
    const { bytes } = await this.bytes(url, { body });
    let value;
    try {
      value = JSON.parse(bytes.toString('utf8'));
    } catch {
      throw new Error('Invalid Kugou response');
    }
    if (!value || typeof value !== 'object' || Array.isArray(value))
      throw new Error('Invalid Kugou response');
    return value;
  }
  async account(route, body) {
    if (!this.key) throw new Error('Set the Kugou account key in plugin settings');
    const result = await this.json(route, {}, { ...body, cookies: this.cookies });
    const updated = result.cookies;
    delete result.cookies;
    if (updated && typeof updated === 'object' && !Array.isArray(updated)) {
      const write = this.sessionWrites.then(async () => {
        const cookies = { ...this.cookies };
        for (const [key, value] of Object.entries(updated))
          if (typeof value === 'string') cookies[key] = value;
        const serialized = JSON.stringify({ version: 1, cookies });
        if (Buffer.byteLength(serialized) > 16384) throw new Error('Kugou session exceeds storage limit');
        await this.context.secrets.set('account-session', serialized);
        if (
          JSON.stringify(
            Object.entries(JSON.parse(await this.context.secrets.get('account-session')).cookies).sort(),
          ) !== JSON.stringify(Object.entries(cookies).sort())
        )
          throw new Error('Cannot verify system keyring session write');
        this.cookies = cookies;
      });
      this.sessionWrites = write.catch(() => {});
      await write;
    }
    return result;
  }
  async search(query, page = 1) {
    if (
      typeof query !== 'string' ||
      !query.trim() ||
      query.length > 200 ||
      !Number.isInteger(page) ||
      page < 1 ||
      page > 1000
    )
      throw new Error('Invalid search keywords or page');
    const body = await this.json('/search', { keywords: query.trim(), type: 'song', page, pagesize: 30 });
    return {
      tracks: parseTracks(body),
      ...(body.data.lists.length === 30 && page < 1000 ? { cursor: String(page + 1) } : {}),
    };
  }
  async resolve(track) {
    if (!validHash(track.id)) throw new Error('Invalid Kugou track ID');
    if (!this.loggedIn()) throw new Error('Log in to Kugou first');
    const body = await this.account('/song/url', {
      hash: track.id,
      quality: '128',
      ...(track.audio_id ? { album_audio_id: track.audio_id } : {}),
    });
    if (!businessOk(body, ['1', '200'])) {
      if (String(body.error_code) === '20028')
        throw new Error('Kugou requires additional account verification');
      if (body.fail_process?.some((item) => ['pkg', 'buy'].includes(item)))
        throw new Error('Kugou account has no playback permission for this song');
      throw new Error('Kugou did not provide playback permission');
    }
    const urls = body.url ?? body.data?.url;
    return {
      url: audioUrl(Array.isArray(urls) ? urls[0] : urls),
      headers: { 'User-Agent': 'Mozilla/5.0 NekoTune/1.0' },
      expiresAt: Date.now() + 5 * 60_000,
      extension: 'mp3',
      maxBytes: 100 * 1024 * 1024,
      allowedHosts: ['*.kugou.com'],
      contentTypes: [
        'audio/mpeg',
        'audio/mp3',
        'audio/flac',
        'audio/x-flac',
        'audio/aac',
        'audio/mp4',
        'audio/x-m4a',
        'audio/ogg',
        'application/ogg',
        'audio/wav',
        'audio/x-wav',
        'audio/webm',
      ],
    };
  }
}

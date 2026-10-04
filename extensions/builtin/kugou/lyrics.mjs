import fs from 'node:fs/promises';
import path from 'node:path';
import { randomUUID } from 'node:crypto';
import { decodeKrc, coverUrl } from './client.mjs';

export async function lyricCandidates(client, track) {
  const result = await client.json('/search/lyric', {
    keywords: track.title,
    hash: track.id,
    man: 'yes',
    duration: track.duration_ms || '',
    album_audio_id: track.audio_id || '',
  });
  if (![200, 404].includes(Number(result.status)) || !Array.isArray(result.candidates))
    throw new Error('Invalid Kugou lyric candidates');
  return result.candidates
    .filter((item) => Number(item.id) > 0 && typeof item.accesskey === 'string' && item.accesskey)
    .slice(0, 100)
    .map((item) => ({
      id: String(item.id),
      accesskey: item.accesskey,
      title: item.song || track.title,
      artist: item.singer || track.artist,
      album: track.album || '',
      duration_ms: Number(item.duration) || track.duration_ms,
      cover_url: track.cover_url || '',
      score: 0,
    }));
}
export async function lyricDocument(client, candidate, includeLrc = false) {
  let krc;
  try {
    krc = decodeKrc(
      await client.json('https://lyrics.kugou.com/download', {
        ver: 1,
        client: 'pc',
        id: candidate.id,
        accesskey: candidate.accesskey,
        fmt: 'krc',
        charset: 'utf8',
      }),
    );
  } catch {
    /* Use LRC for this same selected version. */
  }
  let lrc = '';
  if (!krc || includeLrc) {
    try {
      const response = await client.json('/lyric', {
        id: candidate.id,
        accesskey: candidate.accesskey,
        fmt: 'lrc',
        decode: 1,
      });
      lrc = String(response.decodeContent || '');
    } catch {
      if (!krc) throw new Error('Cannot download selected lyrics');
    }
  }
  if (!krc && !lrc.trim()) throw new Error('Selected lyrics are empty');
  return {
    krc_lyrics: krc?.text || '',
    synced_lyrics: /\[\d+:\d+(?:\.\d+)?\]/.test(lrc) ? lrc : '',
    plain_lyrics: lrc,
    cover_url: candidate.cover_url || '',
    ...(krc ? { binary: krc.binary } : {}),
  };
}
export function chooseAutomatic(candidates, track) {
  const normalized = (value) =>
    String(value || '')
      .normalize('NFKC')
      .toLowerCase()
      .replace(/[\s\p{P}\p{S}]/gu, '');
  const choices = candidates
    .filter(
      (item) =>
        normalized(item.title) === normalized(track.title) &&
        normalized(item.artist) === normalized(track.artist),
    )
    .map((item) => ({
      item,
      delta:
        item.duration_ms > 0 && track.duration_ms > 0 ? Math.abs(item.duration_ms - track.duration_ms) : 0,
    }))
    .filter((item) => item.delta <= 3000)
    .sort((a, b) => a.delta - b.delta);
  return choices.length && (choices.length === 1 || choices[0].delta < choices[1].delta)
    ? choices[0].item
    : null;
}
async function exists(file) {
  try {
    await fs.lstat(file);
    return true;
  } catch (error) {
    if (error.code === 'ENOENT') return false;
    throw error;
  }
}
async function publish(file, bytes) {
  if (await exists(file)) return 'existing';
  const temporary = `${file}.${randomUUID()}.partial`;
  try {
    await fs.writeFile(temporary, bytes, { flag: 'wx', mode: 0o600 });
    try {
      await fs.link(temporary, file);
      return 'saved';
    } catch (error) {
      if (error.code === 'EEXIST') return 'existing';
      throw error;
    }
  } finally {
    await fs.rm(temporary, { force: true });
  }
}
function imageSuffix(bytes, type) {
  const bounded = (width, height) => width > 0 && height > 0 && width <= 4096 && height <= 4096;
  if (
    type.startsWith('image/png') &&
    bytes.length >= 45 &&
    bytes.subarray(0, 8).equals(Buffer.from([137, 80, 78, 71, 13, 10, 26, 10])) &&
    bytes.toString('ascii', 12, 16) === 'IHDR' &&
    bytes.toString('ascii', bytes.length - 8, bytes.length - 4) === 'IEND' &&
    bounded(bytes.readUInt32BE(16), bytes.readUInt32BE(20))
  )
    return 'png';
  if (
    type.startsWith('image/jpeg') &&
    bytes.length > 4 &&
    bytes.readUInt16BE(0) === 0xffd8 &&
    bytes.readUInt16BE(bytes.length - 2) === 0xffd9
  ) {
    let offset = 2;
    while (offset + 4 < bytes.length && bytes[offset] === 0xff) {
      while (bytes[offset] === 0xff) offset++;
      const marker = bytes[offset++];
      if (marker === 0xda || marker === 0xd9) break;
      const length = bytes.readUInt16BE(offset);
      if (length < 2 || offset + length > bytes.length) break;
      if (
        [0xc0, 0xc1, 0xc2].includes(marker) &&
        length >= 8 &&
        bounded(bytes.readUInt16BE(offset + 5), bytes.readUInt16BE(offset + 3))
      )
        return 'jpg';
      offset += length;
    }
  }
  if (
    type.startsWith('image/webp') &&
    bytes.length >= 30 &&
    bytes.toString('ascii', 0, 4) === 'RIFF' &&
    bytes.toString('ascii', 8, 12) === 'WEBP' &&
    bytes.readUInt32LE(4) + 8 === bytes.length
  ) {
    const kind = bytes.toString('ascii', 12, 16);
    if (kind === 'VP8X' && bounded(1 + bytes.readUIntLE(24, 3), 1 + bytes.readUIntLE(27, 3))) return 'webp';
    if (kind === 'VP8L' && bytes[20] === 0x2f) {
      const bits = bytes.readUInt32LE(21);
      if (bounded(1 + (bits & 0x3fff), 1 + ((bits >>> 14) & 0x3fff))) return 'webp';
    }
    if (
      kind === 'VP8 ' &&
      bytes.subarray(23, 26).equals(Buffer.from([0x9d, 0x01, 0x2a])) &&
      bounded(bytes.readUInt16LE(26) & 0x3fff, bytes.readUInt16LE(28) & 0x3fff)
    )
      return 'webp';
  }
  throw new Error('Unsupported or oversized cover response');
}
export async function saveAssets(client, track, audioPath, existing) {
  // Existing songs and user sidecars keep their previous assets and custom lyrics.
  if (existing) return { lyric_status: 'existing', cover_status: 'existing' };
  const base = path.join(path.dirname(audioPath), path.parse(audioPath).name);
  let lyric_status = 'none',
    cover_status = 'none';
  if ((await exists(base + '.krc')) || (await exists(base + '.lrc'))) lyric_status = 'existing';
  else
    try {
      const candidates = await lyricCandidates(client, track),
        selected = chooseAutomatic(candidates, track);
      if (!selected) lyric_status = candidates.length ? 'uncertain' : 'none';
      else {
        const document = await lyricDocument(client, selected, true);
        if (document.binary) lyric_status = await publish(base + '.krc', document.binary);
        if (document.synced_lyrics) {
          const saved = await publish(base + '.lrc', document.synced_lyrics);
          if (lyric_status !== 'saved' && lyric_status !== 'existing') lyric_status = saved;
        }
      }
    } catch {
      lyric_status = 'error';
    }
  if ((await exists(base + '.jpg')) || (await exists(base + '.png')) || (await exists(base + '.webp')))
    cover_status = 'existing';
  else if (coverUrl(track.cover_url))
    try {
      const image = await client.bytes(track.cover_url, {
        limit: 5 * 1024 * 1024,
        accept: 'image/jpeg,image/png,image/webp',
      });
      cover_status = await publish(base + '.' + imageSuffix(image.bytes, image.type), image.bytes);
    } catch {
      cover_status = 'error';
    }
  return { lyric_status, cover_status };
}

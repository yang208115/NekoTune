import http from 'node:http';

function makeAudio(frequency) {
  const sampleRate = 22050,
    seconds = 8,
    samples = sampleRate * seconds;
  const audio = Buffer.alloc(44 + samples * 2);
  audio.write('RIFF');
  audio.writeUInt32LE(audio.length - 8, 4);
  audio.write('WAVEfmt ', 8);
  audio.writeUInt32LE(16, 16);
  audio.writeUInt16LE(1, 20);
  audio.writeUInt16LE(1, 22);
  audio.writeUInt32LE(sampleRate, 24);
  audio.writeUInt32LE(sampleRate * 2, 28);
  audio.writeUInt16LE(2, 32);
  audio.writeUInt16LE(16, 34);
  audio.write('data', 36);
  audio.writeUInt32LE(samples * 2, 40);
  for (let i = 0; i < samples; ++i)
    audio.writeInt16LE(Math.round(Math.sin((2 * Math.PI * frequency * i) / sampleRate) * 1800), 44 + i * 2);
  return audio;
}
export async function activate(context) {
  const audio = [makeAudio(220), makeAudio(330)];
  const server = http.createServer((request, response) => {
    if (request.headers['x-nekotune-example'] !== 'audio-test') {
      response.writeHead(403).end();
      return;
    }
    const buffer = audio[request.url === '/two.wav' ? 1 : 0];
    const match = /^bytes=(\d+)-(\d*)$/.exec(request.headers.range ?? '');
    const start = match ? Number(match[1]) : 0,
      end = match && match[2] ? Math.min(Number(match[2]), buffer.length - 1) : buffer.length - 1;
    if (start >= buffer.length || start > end) {
      response.writeHead(416, { 'Content-Range': `bytes */${buffer.length}` }).end();
      return;
    }
    const headers = {
      'Content-Type': 'audio/wav',
      'Accept-Ranges': 'bytes',
      'Content-Length': end - start + 1,
    };
    if (match) headers['Content-Range'] = `bytes ${start}-${end}/${buffer.length}`;
    response.writeHead(match ? 206 : 200, headers);
    response.end(request.method === 'HEAD' ? undefined : buffer.subarray(start, end + 1));
  });
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  context.subscriptions.push(
    () =>
      new Promise((resolve) => {
        server.closeAllConnections();
        server.close(resolve);
      }),
  );
  const tracks = [
    { id: 'one', title: 'Test tone · 220 Hz', artist: 'NekoTune', duration_ms: 8000 },
    { id: 'two', title: 'Test tone · 330 Hz', artist: 'NekoTune', duration_ms: 8000 },
  ];
  await context.music.register(
    'tones',
    {
      async search({ query = '' }) {
        return { tracks: tracks.filter((track) => track.title.toLowerCase().includes(query.toLowerCase())) };
      },
      async track({ id }) {
        const track = tracks.find((track) => track.id === id);
        if (!track) throw new Error('Track not found');
        return track;
      },
      async resolve({ id }) {
        if (!tracks.some((track) => track.id === id)) throw new Error('Track not found');
        return {
          url: `http://127.0.0.1:${server.address().port}/${id}.wav`,
          headers: { 'X-NekoTune-Example': 'audio-test' },
          expiresAt: Date.now() + 60_000,
          extension: 'wav',
        };
      },
    },
    { name: 'Local test tones / 本地测试音源', download: true },
  );
}

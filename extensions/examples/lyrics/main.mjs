export async function activate(context) {
  await context.lyrics.register(
    'demo',
    {
      async search(query) {
        return {
          candidates: [
            {
              id: 'demo',
              title: query.title,
              artist: query.artist,
              score: 1,
              synced_lyrics:
                '[00:00.00]NekoTune extension lyrics\n[00:02.00]歌词来源可独立安装与重载\n[00:05.00]Enjoy your music',
            },
          ],
        };
      },
      async resolve(candidate) {
        return candidate;
      },
    },
    { name: 'Example / 示例歌词' },
  );
}

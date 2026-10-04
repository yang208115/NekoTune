export async function activate(context) {
  await context.music.register('music', {
    async search() { return { tracks: [] }; },
    async track() { throw new Error('No tracks in this UI fixture'); },
    async resolve() { throw new Error('No audio in this UI fixture'); },
  }, { name: 'Test source', page: 'music' });
  await context.lyrics.register('lyrics', {
    async search() { return { candidates: [] }; },
    async resolve(candidate) { return candidate; },
  }, { name: 'Test lyrics' });
}

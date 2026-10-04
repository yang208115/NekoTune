export async function activate(context) {
  await context.services.register('hello', async () => ({
    message: 'Hello from the independent Node.js extension process!',
  }));
  await context.commands.register(
    'inspect-song',
    async (params) => {
      context.log.info('Selection', params);
      await context.events.emit('selection', params);
      return params;
    },
    { title: 'Inspect selection' },
  );
}

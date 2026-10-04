import type { ExtensionContext } from '../../sdk/index.js';
export async function activate(context: ExtensionContext) {
  let timer: ReturnType<typeof setTimeout> | undefined;
  context.subscriptions.push(() => clearTimeout(timer));
  await context.commands.register(
    'sleep-timer',
    async () => {
      clearTimeout(timer);
      const seconds = Math.max(1, Number(context.config.sleepSeconds) || 30);
      timer = setTimeout(() => {
        void context.host.call('player.pause').catch((error) => context.log.error(error));
      }, seconds * 1000);
      return { seconds };
    },
    { title: 'Sleep timer', shortcut: 'Ctrl+Shift+S' },
  );
  context.events.on('player.track_changed', (event) => context.log.info('Now playing:', event.song?.title));
}

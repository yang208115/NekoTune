import fs from 'node:fs/promises';
import { createWriteStream } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { pipeline } from 'node:stream/promises';
import yazl from 'yazl';
import { readManifest } from '../runtime/packages.mjs';

const [command, target, output] = process.argv.slice(2);
try {
  if (!target)
    throw new Error(
      'Usage: pnpm run validate <directory> | pnpm run pack <directory> [output.zip] | pnpm run create <directory>',
    );
  const directory = path.resolve(target);
  if (command === 'create') {
    const id = path
      .basename(directory)
      .toLowerCase()
      .replace(/[^a-z0-9.-]/g, '-');
    await fs.mkdir(directory, { recursive: false });
    const sdkPath = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../sdk');
    await fs.writeFile(
      path.join(directory, 'extension.json'),
      JSON.stringify({ id, name: id, version: '1.0.0', apiVersion: 1, main: 'dist/main.mjs' }, null, 2),
    );
    await fs.writeFile(
      path.join(directory, 'package.json'),
      JSON.stringify(
        {
          private: true,
          type: 'module',
          packageManager: 'pnpm@11.5.2',
          scripts: {
            build:
              'esbuild main.ts --bundle --platform=node --format=esm --target=node24 --outfile=dist/main.mjs',
            check: 'tsc --noEmit',
          },
          devDependencies: {
            '@nekotune/sdk': `file:${sdkPath}`,
            '@types/node': '24.10.1',
            esbuild: '0.25.12',
            typescript: '5.9.3',
          },
        },
        null,
        2,
      ),
    );
    await fs.writeFile(path.join(directory, 'pnpm-workspace.yaml'), 'allowBuilds:\n  esbuild: true\n');
    await fs.writeFile(
      path.join(directory, 'tsconfig.json'),
      JSON.stringify(
        {
          compilerOptions: {
            target: 'ES2023',
            module: 'NodeNext',
            moduleResolution: 'NodeNext',
            strict: true,
            noEmit: true,
          },
          include: ['main.ts'],
        },
        null,
        2,
      ),
    );
    await fs.writeFile(
      path.join(directory, 'main.ts'),
      "import type { ExtensionContext } from '@nekotune/sdk';\nexport async function activate(context: ExtensionContext) {\n  await context.commands.register('hello', async () => context.host.call('player.status'), { title: 'Hello NekoTune' });\n}\n",
    );
    console.log(`Created ${directory}. Run pnpm install and pnpm build there, then mount it in NekoTune.`);
  } else {
    const manifest = await readManifest(directory);
    if (command === 'validate') console.log(`${manifest.id}@${manifest.version}: valid`);
    else if (command === 'pack') {
      const archive = new yazl.ZipFile();
      const destination = path.resolve(output ?? `${manifest.id}-${manifest.version}.nekotune.zip`);
      const writing = pipeline(archive.outputStream, createWriteStream(destination));
      async function add(folder, relative = '') {
        for (const entry of await fs.readdir(folder, { withFileTypes: true })) {
          if (
            ['node_modules', '.git', '.codex', '.agents'].includes(entry.name) ||
            entry.name.endsWith('.zip') ||
            entry.name.endsWith('.log')
          )
            continue;
          const local = path.join(folder, entry.name),
            name = path.posix.join(relative, entry.name);
          if (entry.isSymbolicLink())
            throw new Error(`Bundle dependencies first; symlink not supported: ${name}`);
          if (entry.isDirectory()) await add(local, name);
          else archive.addFile(local, name);
        }
      }
      await add(directory);
      archive.end();
      await writing;
      console.log(destination);
    } else throw new Error(`Unknown command: ${command}`);
  }
} catch (error) {
  console.error(error.message);
  process.exitCode = 1;
}

import { build } from 'esbuild';
import { mkdir, copyFile } from 'node:fs/promises';
import { execFileSync } from 'node:child_process';
await mkdir('dist', { recursive: true });
for (const entry of ['supervisor', 'worker']) {
  await build({
    entryPoints: [`runtime/${entry}.mjs`],
    outfile: `dist/${entry}.cjs`,
    bundle: true,
    platform: 'node',
    target: 'node24',
    format: 'cjs',
    sourcemap: true,
  });
}
await copyFile('sdk/index.d.ts', 'dist/index.d.ts');
await mkdir('dist/builtin', { recursive: true });
execFileSync(
  process.execPath,
  ['tools/cli.mjs', 'pack', 'builtin/kugou', 'dist/builtin/nekotune.kugou.zip'],
  { stdio: 'inherit' },
);
await build({
  entryPoints: ['examples/automation/main.ts'],
  outfile: 'examples/automation/dist/main.mjs',
  bundle: true,
  platform: 'node',
  target: 'node24',
  format: 'esm',
});

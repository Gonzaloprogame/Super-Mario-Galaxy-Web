// Replays a GX trace (copied from the engine's log) in Chromium with WebGPU through web/bridge/gx_replay.html, and
// reports whether Aurora read the stream to its end and drew its frames.
//   node web/tests/replay_trace.mjs <directory with gx_replay.html/.js/.wasm> <log with [gx-trace] lines> [captura.png]
import { spawn } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const { chromium } = createRequire(import.meta.url)('playwright');
const root = join(dirname(fileURLToPath(import.meta.url)), '..', '..');
const [dir, logFile, shot] = process.argv.slice(2);
const port = 19500 + Math.floor(Math.random() * 400);
const server = spawn('python3', [join(root, 'tools/serve.py'), String(port), join(dir, '..')], { stdio: ['ignore', 'pipe', 'ignore'] });
await new Promise((resolve) => server.stdout.once('data', resolve));
const browser = await chromium.launch({ args: ['--enable-unsafe-webgpu'] });
const page = await browser.newPage();
await page.goto(`http://127.0.0.1:${port}/bridge/gx_replay.html`);
await page.fill('#log', readFileSync(logFile, 'utf8'));
await page.click('#go');
const done = await page.waitForFunction(() => globalThis.replay.fatal || globalThis.replay.error ||
  (globalThis.replay.frames !== null && globalThis.replay.frames === globalThis.replay.expected), null, { timeout: 180000 })
  .then(() => true, () => false);
const replay = await page.evaluate(() => globalThis.replay);
console.log(replay.lines.filter((l) => l.startsWith('[replay]') || l.startsWith('[página]') || /aurora (ERROR|FATAL|WARNING)/.test(l)).join('\n'));
if (shot) {
  await page.waitForTimeout(1500);
  await page.locator('#screen').screenshot({ path: shot });
}
await browser.close();
server.kill();
const ok = done && !replay.fatal && !replay.error && replay.frames === replay.expected;
console.log(ok ? `REPLAY OK: Aurora leyó todo el flujo y dibujó ${replay.frames} de ${replay.expected} fotogramas`
  : `REPLAY FALLA: ${replay.fatal || replay.error || `${replay.frames} de ${replay.expected} fotogramas`}`);
process.exit(ok ? 0 : 1);

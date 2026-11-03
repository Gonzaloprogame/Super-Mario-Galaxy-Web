// Galaxy tour runner (docs/MOTOR_WEB.md, "Gira de galaxias"): runs the web engine with the user's own disc in
// Chromium, visits each stage of a list through the host's tour (?tour=, the game's own stage change) and records, per
// stage, what the log and the page say: the load time, the stalls and whether they resumed, the renderer's errors and
// warnings, whether the game kept drawing, and one screenshot. It stops at the first hang (a stall that does not
// resume within --hang seconds) and says which stage it was, so the next run can start after it.
//
// It does not say that a stage is playable: only that it loaded and drew without stopping. Playing it is a person's
// job. The screenshots are the game's images: they stay in the output folder, which must not be committed or shared.
//
//   node tools/galaxy_tour.mjs --disc <your .rvz> --stages <file> --out <folder> [--nand <file>] [--frames 900]
//        [--hang 180] [--port 18790] [--site build/site]
// The stages file: one "Stage" or "Stage:scenario" per line (the names of the folders under /StageData of the disc).
// Needs tools/build_web.sh first, and Playwright's Chromium (preinstalled here).
import { spawn } from 'node:child_process';
import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const { chromium } = createRequire(import.meta.url)('playwright');
const root = join(dirname(fileURLToPath(import.meta.url)), '..');

const args = {};
for (let i = 2; i < process.argv.length; i += 2) args[process.argv[i].replace(/^--/, '')] = process.argv[i + 1];
if (!args.disc || !args.stages || !args.out) {
  console.error('usage: node tools/galaxy_tour.mjs --disc <rvz> --stages <file> --out <folder> [--nand <file>] ' +
                '[--frames 900] [--hang 180] [--port 18790]');
  process.exit(64);
}
const frames = Number(args.frames || 900);
const hangSeconds = Number(args.hang || 180);
const port = Number(args.port || 18790);
const stops = readFileSync(args.stages, 'utf8').split('\n').map((l) => l.trim()).filter((l) => l && !l.startsWith('#'))
  .map((l) => (l.includes(':') ? l : `${l}:1`));
mkdirSync(join(args.out, 'shots'), { recursive: true });

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const server = spawn('python3', [join(root, 'tools/serve.py'), String(port), args.site || join(root, 'build/site')],
                     { stdio: ['ignore', 'pipe', 'ignore'] });
await new Promise((r) => server.stdout.once('data', r));
const browser = await chromium.launch({ args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan'] });
const page = await browser.newPage({ viewport: { width: 900, height: 1400 } });
const pageErrors = [];
page.on('pageerror', (e) => pageErrors.push(String(e)));

const results = [];
let current = null; // the stop being visited
const finishStop = (outcome) => {
  if (current && !current.outcome) {
    current.outcome = outcome;
    current.seconds = Math.round((Date.now() - current.since) / 1000);
  }
};
const writeReport = (end) => {
  const summary = { disc: args.disc.split('/').pop(), frames, hangSeconds, end, pageErrors, stages: results };
  writeFileSync(join(args.out, 'tour.json'), JSON.stringify(summary, null, 1));
  const rows = results.map((r) => `| ${r.stop} | ${r.outcome || 'sin terminar'} | ${r.load ?? '—'} | ${r.stalls.length} | ` +
    `${r.drew ? 'sí' : 'no'} | ${r.problems.join('; ').replace(/\|/g, '/').slice(0, 180) || '—'} |`);
  writeFileSync(join(args.out, 'tour.md'), [
    `Gira: ${results.length} paradas, ${frames} fotogramas de escena cada una, cuelgue si una parada dura más de ` +
    `${hangSeconds} s. Fin: ${end}.`, '',
    '| Parada | Resultado | Carga (s) | Paradas | Dibuja | Problemas |', '|---|---|---|---|---|---|', ...rows, ''].join('\n'));
};

try {
  const tour = encodeURIComponent(stops.join(','));
  await page.goto(`http://127.0.0.1:${port}/?tour=${tour}&tourframes=${frames}`);
  await page.waitForFunction(() => document.body.dataset.state === 'ready', null, { timeout: 30000 });
  if (args.nand) {
    const bytes = Array.from(readFileSync(args.nand));
    await page.evaluate(async (b) => nandStore.put('RMGE01', new Uint8Array(b)), bytes);
  }
  await page.setInputFiles('#pak', args.disc);
  await page.selectOption('#mode', 'graphics');
  await page.selectOption('#language', '4');
  await page.fill('#seconds', '0');
  await page.click('#run');

  let seen = 0;            // log lines handled
  let lastGx = 0;          // GX bytes in the last report
  let end = '';
  for (;;) {
    await sleep(5000);
    const state = await page.evaluate(() => document.body.dataset.state);
    const lines = (await page.textContent('#log')).split('\n').map((l) => l.replace(/^\[vigía\] /, ''));
    for (const line of lines.slice(seen)) {
      let m;
      if ((m = /^\[host\] tour: stop (\d+) of (\d+), (\S+) scenario (\d+)/.exec(line))) {
        finishStop('cargó y dibujó');
        current = { stop: `${m[3]}:${m[4]}`, since: Date.now(), load: null, stalls: [], problems: [], drew: false,
                    gxAtStart: lastGx, shot: false };
        results.push(current);
      } else if ((m = /^\[host\] load '[^']*' -> '([^']+)': ([\d.]+) s/.exec(line)) && current) {
        current.load = Number(m[2]);
      } else if ((m = /^\[host\] stall: no GX from the game for (\d+) s/.exec(line)) && current) {
        current.stalls.push({ seconds: Number(m[1]), resumed: false, line: line.slice(0, 600) });
        if (Number(m[1]) >= hangSeconds) {
          current.problems.push(`colgado: ${line.slice(0, 400)}`);
          finishStop('colgado');
          end = `cuelgue en ${current.stop}`;
        }
      } else if ((m = /^\[host\] the game's stream resumed after (\d+) s/.exec(line)) && current) {
        const last = current.stalls[current.stalls.length - 1];
        if (last) last.resumed = Number(m[1]);
      } else if (/\[aurora (ERROR|FATAL|WARNING)|^\[host\] the game stopped|^\[página\] (el motor se detuvo|Error en el motor|El motor abortó)/.test(line) && current) {
        current.problems.push(line.slice(0, 300));
      } else if ((m = /^\[host\] report: .* GX (\d+) bytes/.exec(line))) {
        lastGx = Number(m[1]);
        if (current && lastGx > current.gxAtStart) current.drew = true;
      } else if (/^\[host\] tour: done/.test(line)) {
        finishStop('cargó y dibujó');
        end = 'gira terminada';
      }
    }
    // The log ends in a newline: its last element is empty, and the next reading's first new line takes its place
    seen = Math.max(0, lines.length - 1);
    // The log's last lines, to follow a run while it goes (game data: they stay in the output folder)
    writeFileSync(join(args.out, 'log_tail.txt'), lines.slice(-60).join('\n'));
    // One screenshot per stop, once its stage has drawn for a while
    if (current && !current.shot && current.drew && Date.now() - current.since > 60000) {
      current.shot = true;
      const name = current.stop.replace(/[^A-Za-z0-9_-]/g, '_');
      const url = await page.evaluate(() => document.getElementById('screen').toDataURL('image/png'));
      writeFileSync(join(args.out, 'shots', `${name}.png`), Buffer.from(url.split(',')[1], 'base64'));
    }
    if (state === 'error' || state === 'done') {
      if (current) current.problems.push(`la página terminó en "${state}": ${(await page.textContent('#status')).trim()}`);
      finishStop(state === 'error' ? 'error' : 'el motor terminó');
      end = end || `la página terminó (${state})`;
    }
    writeReport(end || 'en curso');
    if (end) {
      // The whole log at the end, however it ended (game data: it stays in the output folder)
      writeFileSync(join(args.out, 'log_end.txt'), lines.join('\n'));
      break;
    }
  }
  console.log(`${end}: ${join(args.out, 'tour.md')}`);
} finally {
  await browser.close().catch(() => {});
  server.kill();
}

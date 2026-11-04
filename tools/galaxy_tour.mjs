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
//        [--hang 180] [--port 18790] [--site build/site] [--mode graphics|diagnostic] [--debug-port N]
// --debug-port: Chromium's remote debugging port, for a profiler during a hang (scratchpad tools, not here).
// --play <seed>: plays badly during each stop (the keyboard: the stick in changing directions, A, B, shake, Z, C), to
// run the stages' gameplay code (moving, colliding, enemies) and not only their loading. The same seed, the same
// presses. It proves nothing about a stage being playable: it looks for what stops the engine while it is played.
// Diagnostic mode: the game at full speed without a renderer, to measure its own loads (nothing is drawn).
// The stages file: one "Stage" or "Stage:scenario" per line (the names of the folders under /StageData of the disc),
// with test events after it if wanted ("Stage:1/actors@300/star@900": star, grandstar, defeat:<model>, actors, kill,
// movieN, shake, mario, luigi, tp:x;y;z, wait, each at a scene frame of the stop), and run settings on lines of their
// own ("grandstars=N", "flag=Name", "player=luigi"): web/host/headless_host.c, tour_parse. The report says, per stop,
// the events the game performed, the scenes it went through and the movie frames it decoded.
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
// A line is a stop ("Stage", "Stage:scenario", with test events after it: "Stage:1/actors@300/star@600") or a run
// setting ("grandstars=3", "flag=Name", "player=luigi"): web/host/headless_host.c, tour_parse
const isSetting = (l) => /^[a-z]+=/.test(l);
const stops = readFileSync(args.stages, 'utf8').split('\n').map((l) => l.trim()).filter((l) => l && !l.startsWith('#'))
  .map((l) => {
    if (isSetting(l)) return l;
    const [head, ...events] = l.split('/');
    return [head.includes(':') ? head : `${head}:1`, ...events].join('/');
  });
mkdirSync(join(args.out, 'shots'), { recursive: true });

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const server = spawn('python3', [join(root, 'tools/serve.py'), String(port), args.site || join(root, 'build/site')],
                     { stdio: ['ignore', 'pipe', 'ignore'] });
await new Promise((r) => server.stdout.once('data', r));
const browser = await chromium.launch({ args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan',
  ...(args['debug-port'] ? [`--remote-debugging-port=${args['debug-port']}`] : [])] });
const page = await browser.newPage({ viewport: { width: 900, height: 1400 } });
const pageErrors = [];
page.on('pageerror', (e) => pageErrors.push(String(e)));

const results = [];
const settings = [];     // the run settings the host applied ([host] test: grand stars / event flag / player)
const tourProblems = []; // what the host could not read in the list
let movieFrames = 0;     // movie frames decoded, from the host's reports
let lastScene = '';      // the scene the game is in ([host] stage:): a stop starts there
let current = null; // the stop being visited
// With graphics, a stop also has to reach the screen: the frames the page painted meanwhile (web/shell/present.js)
const graphics = args.mode !== 'diagnostic';
let painted = 0; // the page's painted frames at the last reading
const finishStop = (outcome) => {
  if (current && !current.outcome) {
    current.painted = painted - current.paintedAtStart;
    if (graphics && current.drew && current.painted === 0) {
      current.problems.push('la página no pintó ningún fotograma');
    }
    current.outcome = outcome;
    current.seconds = Math.round((Date.now() - current.since) / 1000);
  }
};
const writeReport = (end) => {
  const summary = { disc: args.disc.split('/').pop(), frames, hangSeconds, end, pageErrors, settings, tourProblems,
                    stages: results };
  writeFileSync(join(args.out, 'tour.json'), JSON.stringify(summary, null, 1));
  const rows = results.map((r) => `| ${r.stop} | ${r.outcome || 'sin terminar'} | ${r.load ?? '—'} | ${r.stalls.length} | ` +
    `${r.drew ? 'sí' : 'no'}${graphics && r.painted !== undefined ? ` (${r.painted} en pantalla)` : ''} | ` +
    `${r.events.join(', ') || '—'} | ${r.startScene ? `(${r.startScene}) ` : ''}${r.scenes.join(' → ') || '—'} | ${r.movieFrames || 0} | ` +
    `${r.problems.join('; ').replace(/\|/g, '/').slice(0, 180) || '—'} |`);
  writeFileSync(join(args.out, 'tour.md'), [
    `Gira: ${results.length} paradas, ${frames} fotogramas de escena cada una, cuelgue si una parada dura más de ` +
    `${hangSeconds} s${args.mode === 'diagnostic' ? ', modo diagnóstico' : ''}${args.play ? `, jugando a ciegas (semilla ${Number(args.play) || 1})` : ''}. Fin: ${end}.`,
    ...(settings.length ? [`Ajustes de prueba: ${settings.join('; ')}.`] : []),
    ...(tourProblems.length ? [`Problemas de la gira: ${tourProblems.join('; ')}.`] : []), '',
    '| Parada | Resultado | Carga (s) | Paradas | Dibuja | Eventos de prueba | Escenas | Vídeo (fotogramas) | Problemas |',
    '|---|---|---|---|---|---|---|---|---|', ...rows, ''].join('\n'));
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
  await page.selectOption('#mode', args.mode || 'graphics');
  await page.selectOption('#language', '4');
  // The diagnostic mode needs a length: the tour's frames at 60 a second, twice over (for the loads), and at least an
  // hour (a fixed hour ended a long tour of the 44 stages at its 38th)
  const diagnosticSeconds = Math.max(3600, Math.ceil((stops.length * frames * 2) / 60));
  await page.fill('#seconds', args.mode === 'diagnostic' ? String(diagnosticSeconds) : '0');
  await page.click('#run');

  // --play: a press every 0.7 s from a seeded generator (xorshift), until the tour ends
  let playing = !!args.play;
  if (playing) {
    let seed = (Number(args.play) || 1) >>> 0 || 1;
    const random = () => { seed ^= seed << 13; seed >>>= 0; seed ^= seed >>> 17; seed ^= seed << 5; seed >>>= 0; return seed / 4294967296; };
    const sticks = [['KeyW'], ['KeyS'], ['KeyA'], ['KeyD'], ['KeyW', 'KeyA'], ['KeyW', 'KeyD'], ['KeyS', 'KeyA'], ['KeyS', 'KeyD'], []];
    const buttons = ['Space', 'Space', 'ShiftLeft', 'KeyQ', 'KeyZ', 'KeyC'];
    (async () => {
      let held = [];
      while (playing) {
        for (const key of held) await page.keyboard.up(key).catch(() => {});
        held = [...sticks[Math.floor(random() * sticks.length)]];
        if (random() < 0.6) held.push(buttons[Math.floor(random() * buttons.length)]);
        for (const key of held) await page.keyboard.down(key).catch(() => {});
        await sleep(700);
      }
      for (const key of held) await page.keyboard.up(key).catch(() => {});
    })();
  }

  let seen = 0;            // log lines handled
  let lastFullLog = 0;     // when the whole log was last written
  let lastGx = 0;          // GX bytes in the last report
  let end = '';
  for (;;) {
    await sleep(5000);
    const state = await page.evaluate(() => document.body.dataset.state);
    painted = await page.evaluate(() => (globalThis.smgPresenter ? globalThis.smgPresenter.frames : 0));
    const lines = (await page.textContent('#log')).split('\n').map((l) => l.replace(/^\[vigía\] /, ''));
    for (const line of lines.slice(seen)) {
      let m;
      if ((m = /^\[host\] tour: stop (\d+) of (\d+), (\S+) scenario (\d+)/.exec(line))) {
        finishStop('cargó y dibujó');
        current = { stop: `${m[3]}:${m[4]}`, since: Date.now(), load: null, stalls: [], problems: [], drew: false,
                    gxAtStart: lastGx, shot: false, paintedAtStart: painted, events: [], scenes: [],
                    // A stop to the stage the game is already in changes no scene: it stays where it started
                    startScene: lastScene,
                    moviesAtStart: movieFrames, movieFrames: 0 };
        results.push(current);
      } else if ((m = /^\[host\] test: (.+?) at frame (\d+) of stop \d+/.exec(line)) && current) {
        current.events.push(`${m[1]}@${m[2]}`);
      } else if ((m = /^\[host\] test: (grand stars .*|event flag .*|player .*)$/.exec(line))) {
        settings.push(m[1]);
      } else if ((m = /^\[host\] tour: (unknown .*|more than .*)$/.exec(line))) {
        tourProblems.push(m[1]);
      } else if ((m = /^\[host\] stage: (\S+)/.exec(line))) {
        lastScene = m[1];
        if (current && current.scenes[current.scenes.length - 1] !== m[1]) current.scenes.push(m[1]);
      } else if ((m = /^\[host\] movies: (\d+) frames decoded/.exec(line))) {
        movieFrames = Number(m[1]);
        if (current) current.movieFrames = movieFrames - current.moviesAtStart;
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
      } else if (/\[aurora (ERROR|FATAL|WARNING)|^\[host\] the game stopped|^\[host\] memory error|^\[página\] (el motor se detuvo|Error en el motor|El motor abortó)/.test(line) && current) {
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
    // The whole log once a minute too: a run cut off from outside (a time limit) keeps what it saw
    if (Date.now() - lastFullLog > 60000) {
      lastFullLog = Date.now();
      writeFileSync(join(args.out, 'log_full.txt'), lines.join('\n'));
    }
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
  playing = false;
  console.log(`${end}: ${join(args.out, 'tour.md')}`);
} finally {
  await browser.close().catch(() => {});
  server.kill();
}

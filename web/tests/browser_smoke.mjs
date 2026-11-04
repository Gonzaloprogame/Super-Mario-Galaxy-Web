// Prueba de humo en Chromium (Playwright): la página carga el motor en un worker, lee una partición sintética y muestra
// el resultado real del motor, en sus dos modos: diagnóstico (sin renderizador) y con gráficos (Aurora sobre WebGPU,
// que debe arrancar en el worker del motor antes de que el motor llegue al mismo error). Sin aislamiento de origen
// cruzado, coi-serviceworker lo da (como en GitHub Pages); sin él (sin service workers), la página se niega a
// arrancar y lo explica.
//   node web/tests/browser_smoke.mjs   (tras tools/build_web.sh; usa el Chromium preinstalado de Playwright)
import { spawn, execFileSync } from 'node:child_process';
import { mkdtempSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createRequire } from 'node:module';
import http from 'node:http';
import { readFileSync, existsSync } from 'node:fs';

const require = createRequire(import.meta.url);
const { chromium } = require('playwright');
const root = join(dirname(fileURLToPath(import.meta.url)), '..', '..');
// SMG_SITE: another copy of the site (to test a build while build/site is in use)
const site = process.env.SMG_SITE || join(root, "build", "site");
const fixtures = mkdtempSync(join(tmpdir(), 'smg-fixtures-'));
execFileSync('python3', ['-I', join(root, 'web/tests/make_fixture.py'), fixtures]);

let failures = 0;
const check = (ok, what) => {
  console.log(`${ok ? 'PASS' : 'FAIL'} ${what}`);
  if (!ok) failures++;
};

async function withServer(script, fn) {
  const port = 18000 + Math.floor(Math.random() * 1000);
  const server = spawn('python3', [join(root, 'tools/serve.py'), String(port), site], { stdio: ['ignore', 'pipe', 'inherit'] });
  await new Promise((resolve) => server.stdout.once('data', resolve));
  try {
    await fn(`http://127.0.0.1:${port}/`);
  } finally {
    server.kill();
  }
}

// A server without COOP/COEP: the same files, no isolation.
async function withPlainServer(fn) {
  const server = http.createServer((req, res) => {
    const path = join(site, req.url === '/' ? 'index.html' : req.url.split('?')[0]);
    if (!existsSync(path)) { res.writeHead(404); res.end(); return; }
    const type = path.endsWith('.js') ? 'text/javascript' : path.endsWith('.wasm') ? 'application/wasm' : 'text/html';
    res.writeHead(200, { 'Content-Type': type });
    res.end(readFileSync(path));
  });
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  try {
    await fn(`http://127.0.0.1:${server.address().port}/`);
  } finally {
    server.close();
  }
}

// options: language (the page's select), dropWorkerExit (the worker's 'exit' message, Emscripten's onExit, never
// reaches the page: as in the real game's first stop, where Emscripten's exit did not end the runtime)
async function run(url, pak, timeoutMs, mode = 'diagnostic', contextOptions = {}, options = {}) {
  const browser = await chromium.launch({ args: ['--enable-unsafe-webgpu'] });
  const page = await (await browser.newContext(contextOptions)).newPage();
  if (options.dropWorkerExit) {
    await page.addInitScript(() => {
      const NativeWorker = globalThis.Worker;
      globalThis.Worker = class extends NativeWorker {
        set onmessage(handler) {
          super.onmessage = (event) => { if (event.data?.type !== 'exit') handler(event); };
        }
      };
    });
  }
  if (!pak && contextOptions.serviceWorkers !== 'block') {
    // Without COOP/COEP from the server, coi-serviceworker registers and reloads the page once
    await page.goto(url);
    await page.waitForFunction(() => self.crossOriginIsolated === true, null, { timeout: 30000 }).catch(() => {});
  }
  const consoleErrors = [];
  page.on('pageerror', (e) => consoleErrors.push(String(e)));
  await page.goto(url);
  const isolated = await page.evaluate(() => self.crossOriginIsolated);
  let state = await page.evaluate(() => document.body.dataset.state);
  const result = { isolated, state, status: await page.textContent('#status'), log: '', consoleErrors };
  if (pak && state === 'ready') {
    if (options.before) await options.before(page);
    await page.setInputFiles('#pak', pak);
    await page.selectOption('#mode', mode);
    if (options.language !== undefined) await page.selectOption('#language', String(options.language));
    await page.fill('#seconds', '2');
    await page.click('#run');
    // A run that never ends is a result too: its state stays 'running'
    await page.waitForFunction(() => ['done', 'error'].includes(document.body.dataset.state), null, { timeout: timeoutMs })
      .catch(() => {});
    result.state = await page.evaluate(() => document.body.dataset.state);
    result.status = await page.textContent('#status');
    result.log = await page.textContent('#log');
    if (options.after) result.after = await options.after(page);
  }
  await browser.close();
  return result;
}

await withServer('isolated', async (url) => {
  const r = await run(url, join(fixtures, 'fixture_emptydol.pak'), 120000);
  check(r.isolated === true, 'la página servida con COOP/COEP está aislada (crossOriginIsolated)');
  check(r.state === 'error', `el motor se detiene con error ante un main.dol vacío (estado: ${r.state})`);
  check(/RMGE01, 16384 bytes/.test(r.log), 'el host leyó la partición a través de WORKERFS');
  check(/running the game/.test(r.log), 'el módulo de juego se acopló al host y arrancó');
  check(/cStoryProgressTable .* is not in the disc's main\.dol/.test(r.log),
        'el módulo de juego (wasm) ejecutó su cargador de datos del DOL y explicó el fallo');
  check(/código 3/.test(r.status), `la interfaz muestra el código de salida real del motor: "${r.status.trim()}"`);
  const pool = /thread pool: (\d+) workers for the host's (\d+) threads/.exec(r.log);
  check(pool && Number(pool[1]) >= Number(pool[2]),
        `sin renderizador: hay un worker para cada hilo del host (${pool ? pool[0] : 'sin línea'}); uno de más no arrancaría`);
  check(/\[host\] report:/.test(r.log) && !/memory error/.test(r.log),
        'un arranque normal no escribe por debajo de la dirección 1024 (ninguna escritura por puntero nulo)');
  check(r.consoleErrors.length === 0, `sin excepciones de página (${r.consoleErrors.join('; ')})`);
  console.log('--- registro del motor (aislado) ---\n' + r.log.trim());

  const w = await run(url, join(fixtures, 'fixture_wrongid.pak'), 120000);
  check(w.state === 'error' && /not Super Mario Galaxy/.test(w.log), 'una partición de otro juego se rechaza');

  // With graphics: the same engine and host with Aurora (smg_web.js)
  const g = await run(url, join(fixtures, 'fixture_emptydol.pak'), 180000, 'graphics');
  check(/RMGE01, 16384 bytes/.test(g.log), 'con gráficos: el host leyó la partición a través de WORKERFS');
  check(/\[host\] Aurora is drawing on WebGPU/.test(g.log),
        'con gráficos: Aurora arrancó sobre WebGPU en el worker del motor');
  check(/cStoryProgressTable .* is not in the disc's main\.dol/.test(g.log),
        'con gráficos: el módulo de juego llegó al mismo punto (cargador de datos del DOL)');
  check(g.state === 'error' && /código 3/.test(g.status), `con gráficos: código de salida real: "${g.status.trim()}"`);
  check(g.consoleErrors.length === 0, `con gráficos: sin excepciones de página (${g.consoleErrors.join('; ')})`);
  console.log('--- registro del motor (con gráficos) ---\n' + g.log.trim());

  // The console's language: kept if the disc's file table has its folder (UsSpanish), else English (no UsFrench)
  const es = await run(url, join(fixtures, 'fixture_languages.pak'), 120000, 'diagnostic', {}, { language: 4 });
  check(/system language: Spanish \(the disc's UsSpanish folder\)/.test(es.log),
        'idioma: el español elegido en la página llega al juego (la carpeta UsSpanish está en el disco)');
  const fr = await run(url, join(fixtures, 'fixture_languages.pak'), 120000, 'diagnostic', {}, { language: 3 });
  check(/language 3 is not on this disc \(no UsFrench folder\)/.test(fr.log) &&
        /system language: English \(the disc's UsEnglish folder\)/.test(fr.log),
        'idioma: uno que el disco no tiene se cambia por el inglés, y el registro lo dice');

  // A hung game's thread (?hangtest=1, web/host/headless_host.c): the log reaches the page through that thread, so it
  // goes silent; the host's reports still reach the page through the watch log in shared memory ("[vigía]" lines)
  {
    const browser = await chromium.launch({ args: ['--enable-unsafe-webgpu'] });
    const page = await (await browser.newContext()).newPage();
    await page.goto(`${url}?hangtest=1`);
    await page.setInputFiles('#pak', join(fixtures, 'fixture_emptydol.pak'));
    await page.selectOption('#mode', 'diagnostic');
    await page.fill('#seconds', '3600'); // the diagnostic mode needs a length; the run never gets there
    await page.click('#run');
    const seen = await page.waitForFunction(() => /\[vigía\] \[host\] stall:/.test(document.getElementById('log').textContent),
      null, { timeout: 90000 }).then(() => true, () => false);
    const hungLog = await page.textContent('#log');
    const hungStatus = await page.textContent('#status');
    check(/hang test: the game's thread stops answering/.test(hungLog), 'bloqueo de prueba: el hilo del juego deja de responder');
    check(seen, 'bloqueo: el informe de bloqueo llega a la página por memoria compartida (líneas «[vigía]»)');
    check(/\[vigía\] \[host\] stall: .*inside the host's hang test for \d+ ms/.test(hungLog),
          'bloqueo: el informe dice en qué servicio del host está el hilo del juego, y desde cuándo');
    check(/no responde/.test(hungStatus), `bloqueo: la página lo dice en su estado ("${hungStatus.trim().slice(0, 60)}…")`);
    check(/\[vigía\] \[host\] stall: .*a probe sent [\d.]+ s ago was not run/.test(hungLog),
          'bloqueo en un bucle sin ceder: la sonda no se ejecuta, y el aviso lo dice');
    check(/\[vigía\] \[host\] stall: .*the game's last calls to the host: hang test \(1 times, the last \d+ ms ago\)/.test(hungLog),
          'bloqueo: el aviso dice las últimas llamadas del juego al host, y hace cuánto (dónde estaba antes de parar)');
    await browser.close();
  }
  // The same hang in a wait that yields (?hangtest=yield), as Nebula's scheduler sleeping for want of work: the probe runs
  {
    const browser = await chromium.launch({ args: ['--enable-unsafe-webgpu'] });
    const page = await (await browser.newContext()).newPage();
    await page.goto(`${url}?hangtest=yield`);
    await page.setInputFiles('#pak', join(fixtures, 'fixture_emptydol.pak'));
    await page.selectOption('#mode', 'diagnostic');
    await page.fill('#seconds', '3600');
    await page.click('#run');
    await page.waitForFunction(() => /\[vigía\] \[host\] stall:/.test(document.getElementById('log').textContent),
      null, { timeout: 90000 }).catch(() => {});
    const waitLog = await page.textContent('#log');
    check(/\[vigía\] \[host\] stall: .*a probe sent [\d.]+ s ago was run: the game's thread waits/.test(waitLog),
          'bloqueo en una espera que cede: la sonda se ejecuta, y el aviso lo dice');
    await browser.close();
  }

  // The engine dying of a WebAssembly trap (?hangtest=trap: a call through a function pointer that is not in the table,
  // as a corrupt object fails): the page stops with the error, and the log has its stack, to say where it was
  {
    const browser = await chromium.launch({ args: ['--enable-unsafe-webgpu'] });
    const page = await (await browser.newContext()).newPage();
    await page.goto(`${url}?hangtest=trap`);
    await page.setInputFiles('#pak', join(fixtures, 'fixture_emptydol.pak'));
    await page.selectOption('#mode', 'diagnostic');
    await page.fill('#seconds', '60');
    await page.click('#run');
    await page.waitForFunction(() => document.body.dataset.state === 'error', null, { timeout: 90000 }).catch(() => {});
    const trapLog = await page.textContent('#log');
    const trapStatus = await page.textContent('#status');
    check(/trap test: calling a function that does not exist/.test(trapLog) && /table index is out of bounds/.test(trapStatus),
          `error del motor: la página se detiene y dice cuál ("${trapStatus.trim().slice(0, 70)}…")`);
    check(/\[página\] Error en el motor: RuntimeError: table index is out of bounds; pila:\n[^\n]*\n\s*at /.test(trapLog) &&
          /\.wasm/.test(trapLog.split('pila:')[1] || ''),
          'error del motor: el registro tiene su pila, con las funciones de WebAssembly');
    await browser.close();
  }

  // A write through a null pointer (?hangtest=nullwrite, web/host/headless_host.c), which wasm does not stop: the host's
  // report must say it, as it would have said the pipeline export's writes at address 0 (CocoonExGalaxy's crash)
  {
    const browser = await chromium.launch({ args: ['--enable-unsafe-webgpu'] });
    const page = await (await browser.newContext()).newPage();
    await page.goto(`${url}?hangtest=nullwrite`);
    await page.setInputFiles('#pak', join(fixtures, 'fixture_emptydol.pak'));
    await page.selectOption('#mode', 'diagnostic');
    await page.fill('#seconds', '60');
    await page.click('#run');
    await page.waitForFunction(() => ['done', 'error'].includes(document.body.dataset.state), null, { timeout: 90000 })
      .catch(() => {});
    const nullLog = await page.textContent('#log');
    check(/null write test: writing 32 bytes at address 64/.test(nullLog) &&
          /\[host\] memory error: 32 bytes below address 1024 have been written, the first at 64 \(a0 a1 a2 /.test(nullLog),
          'una escritura por puntero nulo: el informe del host la detecta y dice dónde');
    await browser.close();
  }

  // The loading screen (web/shell/app.js, loading): shown at the start with the real steps, gone once the game runs.
  // With no list of shaders kept, it says so; a list it cannot read is rejected and the game still starts; a list whose
  // preload did not finish last time is dropped before the start (Aurora patch 0026).
  {
    const first = await run(url, join(fixtures, 'fixture_emptydol.pak'), 180000, 'graphics', {}, {
      before: async (page) => {
        await page.evaluate(() => {
          new MutationObserver(() => {
            if (!document.getElementById('loading').hidden) globalThis.loadingSeen = true;
          }).observe(document.getElementById('loading'), { attributes: true });
        });
      },
      after: async (page) => page.evaluate(() => ({ seen: globalThis.loadingSeen === true, state: globalThis.smgLoading,
        hidden: document.getElementById('loading').hidden, note: document.getElementById('loading-note').textContent })),
    });
    const f = first.after;
    check(f.seen && f.hidden && f.state && f.state.shown === false,
          `pantalla de carga: aparece al arrancar y se va cuando empieza el juego (${JSON.stringify({ seen: f.seen, hidden: f.hidden })})`);
    check(/Primera vez en este navegador/.test(f.note) && f.state.steps['step-shaders'] === 'Sin shaders guardados todavía',
          `pantalla de carga sin lista guardada: lo dice ("${f.state.steps['step-shaders']}")`);
    const corrupt = await run(url, join(fixtures, 'fixture_emptydol.pak'), 180000, 'graphics', {}, {
      before: async (page) => page.evaluate(() => nandStore.put('RMGE01.pipelines', new Uint8Array([1, 2, 3, 4, 5, 6, 7, 8, 9]))),
    });
    check(/pipelines kept from earlier sessions: 0 accepted \(1 rejected\)/.test(corrupt.log) && /running the game/.test(corrupt.log),
          'una lista de shaders ilegible se rechaza y el juego arranca igual');
    const interrupted = await run(url, join(fixtures, 'fixture_emptydol.pak'), 180000, 'graphics', {}, {
      before: async (page) => page.evaluate(async () => {
        await nandStore.put('RMGE01.pipelines', new Uint8Array([1, 2, 3]));
        localStorage.setItem('smg-preload-pending-RMGE01', '1');
      }),
      after: async (page) => page.evaluate(async () => ({ kept: await nandStore.get('RMGE01.pipelines'),
        mark: localStorage.getItem('smg-preload-pending-RMGE01') })),
    });
    check(/la precarga de shaders anterior no terminó: se descarta/.test(interrupted.log) &&
          !/pipelines kept by the page/.test(interrupted.log) && !interrupted.after.kept && !interrupted.after.mark,
          'una precarga que no terminó: la lista se descarta antes de arrancar y la marca se borra');
  }

  // The controls: the engine gives the page its control words (in its shared memory, marked "SMGI" by the host);
  // the keyboard and the touch controls write them as web/host/console_services.h says
  const c = await run(url, join(fixtures, 'fixture_emptydol.pak'), 120000, 'graphics', {}, {
    after: async (page) => {
      const marked = await page.evaluate(() => smgControls.words !== null && smgControls.words[7] === 0x534D4749);
      await page.evaluate(() => smgControls.attach(smgControls.words));
      const words = () => page.evaluate(() => Array.from(smgControls.words.slice(0, 7)));
      const results = { marked };
      await page.keyboard.down('Space');
      await page.keyboard.down('KeyD');
      await page.keyboard.down('KeyC');
      results.keys = await words();
      await page.keyboard.up('Space');
      await page.keyboard.up('KeyD');
      await page.keyboard.up('KeyC');
      results.released = await words();
      await page.locator('.pad-b').scrollIntoViewIfNeeded();
      const box = await page.locator('.pad-b').boundingBox();
      await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
      await page.mouse.down();
      results.touchB = await words();
      await page.mouse.up();
      await page.locator('#screen').scrollIntoViewIfNeeded();
      const screen = await page.locator('#screen').boundingBox();
      await page.mouse.move(screen.x + screen.width * 0.75, screen.y + screen.height * 0.25);
      results.pointer = await words();
      // A gamepad in the standard mapping (the Gamepad API, stubbed): A and LT held, the left stick up and to the right,
      // the right stick to the left for a while; then all released
      await page.evaluate(() => {
        const button = (pressed) => ({ pressed, value: pressed ? 1 : 0 });
        globalThis.fakePad = { id: 'prueba', index: 0, connected: true, mapping: 'standard',
          buttons: Array.from({ length: 17 }, (_, i) => button(i === 0 || i === 6)), axes: [0.7, -0.7, -1, 0] };
        navigator.getGamepads = () => [globalThis.fakePad];
        dispatchEvent(new Event('gamepadconnected'));
      });
      await page.waitForTimeout(400);
      results.pad = await words();
      await page.evaluate(() => {
        globalThis.fakePad.buttons.forEach((b) => { b.pressed = false; b.value = 0; });
        globalThis.fakePad.axes = [0, 0, 0, 0];
      });
      await page.waitForTimeout(200);
      results.padReleased = await words();
      results.padCount = await page.evaluate(() => smgControls.pads.connected);
      await page.evaluate(() => smgControls.detach());
      return results;
    },
  });
  const k = c.after;
  const graphicsPool = /thread pool: (\d+) workers for the host's (\d+) threads/.exec(c.log);
  check(graphicsPool && Number(graphicsPool[1]) >= Number(graphicsPool[2]),
        `con gráficos: hay un worker para cada hilo del host (${graphicsPool ? graphicsPool[0] : 'sin línea'})`);
  check(k.marked, 'controles: la página recibe las palabras de control del motor (marca "SMGI" del host)');
  check(k.keys[0] === (0x0800 | 0x4000) && k.keys[1] === 100 && k.keys[2] === 0,
        `controles: espacio = A, C = C, D = palanca a la derecha (${k.keys.slice(0, 3)})`);
  check(k.released.slice(0, 3).every((v) => v === 0), 'controles: al soltar, nada pulsado y la palanca al centro');
  check(k.touchB[0] === 0x0400, `controles: el botón B de la pantalla pulsa B (${k.touchB[0]})`);
  check(k.pointer[3] === 1 && Math.abs(k.pointer[4] - 5000) < 100 && Math.abs(k.pointer[5] + 5000) < 100,
        `controles: el puntero sigue al ratón sobre la pantalla (${k.pointer.slice(3, 6)})`);
  check(k.padCount === 1 && k.pad[0] === (0x0800 | 0x2000) && k.pad[1] > 40 && k.pad[2] > 40,
        `mando: A y LT pulsan A y Z, la palanca izquierda mueve la del Nunchuk (${k.pad.slice(0, 3)})`);
  check(k.pad[3] === 1 && k.pad[4] < k.pointer[4] - 1000,
        `mando: la palanca derecha mueve el puntero (de ${k.pointer[4]} a ${k.pad[4]})`);
  check(k.padReleased[0] === 0 && k.padReleased[1] === 0 && k.padReleased[2] === 0,
        `mando: al soltarlo, nada pulsado y la palanca al centro (${k.padReleased.slice(0, 3)})`);

  // The NAND kept between sessions (IndexedDB): a copy kept for the disc reaches the engine when it starts, and a copy
  // the engine hands over (on the presenter's channel) is kept
  const n = await run(url, join(fixtures, 'fixture_emptydol.pak'), 120000, 'diagnostic', {}, {
    before: (page) => page.evaluate(async () => {
      // "SMGN", 1 node: a 4-byte file in the game's save folder (web/host/console_services.c)
      const path = new TextEncoder().encode('/title/00010000/524d4745/data/GameData.bin');
      const blob = new Uint8Array(8 + 7 + path.length + 4);
      const view = new DataView(blob.buffer);
      blob.set(new TextEncoder().encode('SMGN'), 0);
      view.setUint32(4, 1, true);
      view.setUint16(9, path.length, true);
      blob.set(path, 11);
      view.setUint32(11 + path.length, 4, true);
      blob.set([1, 2, 3, 4], 15 + path.length);
      await nandStore.put('RMGE01', blob);
    }),
    after: async (page) => {
      await page.evaluate(() => new BroadcastChannel('smg-present').postMessage({ type: 'nand', id: 'TEST01', data: new Uint8Array([9, 8, 7]) }));
      await new Promise((r) => setTimeout(r, 500));
      return page.evaluate(async () => Array.from((await nandStore.get('TEST01')) || []));
    },
  });
  check(/partida guardada de RMGE01 en este navegador: \d+ bytes/.test(n.log) &&
        /NAND restored from the page: 1 folders and files/.test(n.log),
        'NAND: la copia guardada en el navegador llega al motor al arrancar');
  check(n.after.join(',') === '9,8,7', `NAND: la copia que da el motor se guarda en el navegador (${n.after})`);

  // The engine's end heard from the host itself (its announcement on the presenter's channel), without Emscripten's
  check(/\[host\] exit 3/.test(g.log), 'el host anuncia su salida (código 3)');
  const d = await run(url, join(fixtures, 'fixture_emptydol.pak'), 60000, 'graphics', {}, { dropWorkerExit: true });
  check(d.state === 'error' && /código 3/.test(d.status),
        `sin el onExit de Emscripten, la página termina por el anuncio del host: "${d.status.trim()}"`);

  // Frames in shared memory (web/shell/present.js, attachMemory): the engine hands its slots over and the page marks
  // them read; a frame posted by slot is painted from there, and one whose slot was rewritten since is dropped
  const f = await run(url, join(fixtures, 'fixture_emptydol.pak'), 60000, 'graphics', {}, {
    after: (page) => page.evaluate(async () => {
      const p = globalThis.smgPresenter;
      const engine = p.memory && { shared: p.memory.buffer instanceof SharedArrayBuffer,
                                   marked: Atomics.load(new Int32Array(p.memory.buffer, p.memory.address, 1), 0) };
      const HEADER = 16, SLOT = HEADER + 640 * 576 * 4, address = 64;
      const buffer = new SharedArrayBuffer(address + HEADER + 3 * SLOT);
      p.attachMemory(buffer, address);
      const marked = Atomics.load(new Int32Array(buffer, address, 1), 0);
      const base = address + HEADER + SLOT; // slot 1
      new Int32Array(buffer, base, 1)[0] = 6;
      const rows = new Uint8Array(buffer, base + HEADER, 8 * 4 * 4);
      for (let i = 0; i < 32; i++) rows.set([200, 40, 10, 255], i * 4);
      const channel = new BroadcastChannel('smg-present');
      const before = { frames: p.frames, shared: p.sharedFrames, dropped: p.dropped };
      channel.postMessage({ type: 'frame', width: 8, height: 4, black: 0, slot: 1, sequence: 6 });
      await new Promise((r) => setTimeout(r, 300));
      const canvas = document.getElementById('screen');
      const pixel = Array.from(canvas.getContext('2d').getImageData(3, 2, 1, 1).data);
      const painted = { frames: p.frames, shared: p.sharedFrames, size: `${canvas.width}x${canvas.height}` };
      channel.postMessage({ type: 'frame', width: 8, height: 4, black: 0, slot: 1, sequence: 8 });
      await new Promise((r) => setTimeout(r, 300));
      return { engine, marked, before, pixel, painted, after: { frames: p.frames, dropped: p.dropped } };
    }),
  });
  check(f.after && f.after.engine && f.after.engine.shared && f.after.engine.marked === 1,
        `fotogramas en memoria compartida: el motor da sus huecos y la página los marca leídos (${JSON.stringify(f.after && f.after.engine)})`);
  check(f.after && f.after.marked === 1 && f.after.painted.shared === f.after.before.shared + 1 &&
        f.after.painted.frames === f.after.before.frames + 1 && f.after.painted.size === '8x4' &&
        f.after.pixel.join(',') === '200,40,10,255',
        `fotogramas en memoria compartida: uno anunciado por su hueco se pinta desde allí (${JSON.stringify(f.after && { pixel: f.after.pixel, painted: f.after.painted })})`);
  check(f.after && f.after.after.frames === f.after.painted.frames && f.after.after.dropped === f.after.before.dropped + 1,
        `fotogramas en memoria compartida: uno cuyo hueco se reescribió se descarta (${JSON.stringify(f.after && f.after.after)})`);

  // An RVZ disc image (synthetic, from tools/rvz_fixture via web/tests/run_rvz_tests.sh): its game partition is read in
  // the page, without game.pak
  const rvz = join(root, 'build/rvz-tests/rvz_zstd_128k.rvz');
  if (existsSync(rvz)) {
    const v = await run(url, rvz, 180000, 'graphics');
    check(/an RVZ image of RMGE01; its game partition is read in place/.test(v.log),
          'RVZ: el motor reconoce la imagen y lee su partición en la página');
    check(/RMGE01, 5079040 bytes, main\.dol 256 bytes, file table 16 bytes/.test(v.log),
          'RVZ: boot.bin, main.dol y la tabla de archivos leídos de la imagen comprimida');
    check(/cStoryProgressTable .* is not in the disc's main\.dol/.test(v.log),
          'RVZ: el módulo de juego llegó al mismo punto que con game.pak');
    check(v.state === 'error' && /código 3/.test(v.status), `RVZ: código de salida real: "${v.status.trim()}"`);
  } else {
    console.log('SKIP RVZ: falta build/rvz-tests (ejecuta web/tests/run_rvz_tests.sh)');
  }
});

await withPlainServer(async (url) => {
  // Like GitHub Pages: the service worker gives the isolation
  const s = await run(url, null, 30000);
  check(s.isolated === true && s.state === 'ready', `sin COOP/COEP del servidor, coi-serviceworker aísla la página (${s.state})`);
  // Without service workers, nothing can: the page refuses and says why
  const r = await run(url, null, 10000, 'diagnostic', { serviceWorkers: 'block' });
  check(r.isolated === false && r.state === 'unsupported', 'sin COOP/COEP la página no arranca el motor');
  check(/SharedArrayBuffer/.test(r.status), `y explica el motivo: "${r.status.trim().slice(0, 90)}…"`);
});

console.log(failures ? `${failures} fallos` : 'todas las comprobaciones pasaron');
process.exit(failures ? 1 : 0);

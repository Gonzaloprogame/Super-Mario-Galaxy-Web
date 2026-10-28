// Prueba de humo en Chromium (Playwright): la página carga el motor en un worker, lee una partición sintética y muestra
// el resultado real del motor; sin aislamiento de origen cruzado, la página se niega a arrancar y lo explica.
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
const site = join(root, 'build', 'site');
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

async function run(url, pak, timeoutMs) {
  const browser = await chromium.launch();
  const page = await browser.newPage();
  const consoleErrors = [];
  page.on('pageerror', (e) => consoleErrors.push(String(e)));
  await page.goto(url);
  const isolated = await page.evaluate(() => self.crossOriginIsolated);
  let state = await page.evaluate(() => document.body.dataset.state);
  const result = { isolated, state, status: await page.textContent('#status'), log: '', consoleErrors };
  if (pak && state === 'ready') {
    await page.setInputFiles('#pak', pak);
    await page.fill('#seconds', '2');
    await page.click('#run');
    await page.waitForFunction(() => ['done', 'error'].includes(document.body.dataset.state), null, { timeout: timeoutMs });
    result.state = await page.evaluate(() => document.body.dataset.state);
    result.status = await page.textContent('#status');
    result.log = await page.textContent('#log');
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
  check(r.consoleErrors.length === 0, `sin excepciones de página (${r.consoleErrors.join('; ')})`);
  console.log('--- registro del motor (aislado) ---\n' + r.log.trim());

  const w = await run(url, join(fixtures, 'fixture_wrongid.pak'), 120000);
  check(w.state === 'error' && /not Super Mario Galaxy/.test(w.log), 'una partición de otro juego se rechaza');
});

await withPlainServer(async (url) => {
  const r = await run(url, null, 10000);
  check(r.isolated === false && r.state === 'unsupported', 'sin COOP/COEP la página no arranca el motor');
  check(/SharedArrayBuffer/.test(r.status), `y explica el motivo: "${r.status.trim().slice(0, 90)}…"`);
});

console.log(failures ? `${failures} fallos` : 'todas las comprobaciones pasaron');
process.exit(failures ? 1 : 0);

// Prueba del puente FIFO (docs/PUENTE_FIFO.md): en Chromium con WebGPU, la GX del propio juego (el SDK del módulo de
// juego) dibuja una escena de prueba; su flujo FIFO pasa por el host a Aurora, que la dibuja fuera de pantalla, y se
// leen tres puntos del EFB: el color de borrado en una esquina y en el fondo, el color del triángulo en su centro.
//   node web/tests/bridge_smoke.mjs <directorio con gx_bridge.html/.js/.wasm>
import { spawn } from 'node:child_process';
import { createRequire } from 'node:module';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const require = createRequire(import.meta.url);
const { chromium } = require('playwright');
const root = join(dirname(fileURLToPath(import.meta.url)), '..', '..');
const dir = process.argv[2];

let failures = 0;
const check = (ok, what) => { console.log(`${ok ? 'PASS' : 'FAIL'} ${what}`); if (!ok) failures++; };

const port = 19000 + Math.floor(Math.random() * 1000);
const server = spawn('python3', [join(root, 'tools/serve.py'), String(port), dir], { stdio: ['ignore', 'pipe', 'ignore'] });
await new Promise((resolve) => server.stdout.once('data', resolve));

const browser = await chromium.launch({ args: ['--enable-unsafe-webgpu'] });
const page = await browser.newPage();
const consoleLines = [];
const verbose = process.env.SMG_VERBOSE === '1';
const record = (line) => { consoleLines.push(line); if (verbose) console.log(`${Date.now() % 100000} ${line}`); };
page.on('console', (m) => record(`[${m.type()}] ${m.text()}`));
page.on('pageerror', (e) => record(`[pageerror] ${e}`));
await page.goto(`http://127.0.0.1:${port}/gx_bridge.html`);

let state = null;
const deadline = Date.now() + 180000;
while (Date.now() < deadline) {
  state = await Promise.race([
    page.evaluate(() => globalThis.bridge),
    new Promise((resolve) => setTimeout(() => resolve('stuck'), 10000)),
  ]);
  if (state === 'stuck' || (state && (state.exit !== null || state.result))) break;
  await new Promise((r) => setTimeout(r, 500));
}
const lines = state && state !== 'stuck' ? state.lines : [];
console.log(lines.filter((l) => l.startsWith('[bridge]') || l.startsWith('[página]')).join('\n'));
check(state !== 'stuck', 'la página sigue respondiendo');
check(state && state.exit === null, `el motor no abortó (${state && state.exit})`);
const r = state && state !== 'stuck' ? state.result : null;
console.log('resultado:', JSON.stringify(r));
const rgb = (v) => [(v >>> 16) & 255, (v >>> 8) & 255, v & 255];
const near = (got, want, tol = 6) => want.every((v, i) => Math.abs(got[i] - v) <= tol);
check(!!r && !r.error, `Aurora devolvió la lectura del EFB (${r && r.error ? r.error : 'sí'})`);
if (r && !r.error) {
  check(r.frames > 0, `fotogramas GX dibujados por Aurora: ${r.frames}`);
  check(near(rgb(r.corner), [24, 32, 96]), `EFB esquina = color de borrado (24, 32, 96): ${rgb(r.corner)}`);
  check(near(rgb(r.outside), [24, 32, 96]), `EFB fuera del triángulo = color de borrado: ${rgb(r.outside)}`);
  check(near(rgb(r.centre), [240, 176, 16]), `EFB centro del triángulo = color de vértice (240, 176, 16): ${rgb(r.centre)}`);
  check(near(rgb(r.indexed), [32, 200, 64]),
        `EFB triángulo de arrays indexados (direcciones físicas, big-endian) = (32, 200, 64): ${rgb(r.indexed)}`);
  check(near(rgb(r.displayList), [200, 48, 160]),
        `EFB triángulo de la lista de dibujo (GXCallDisplayList) = (200, 48, 160): ${rgb(r.displayList)}`);
  check(near(rgb(r.texLeft), [255, 0, 255]),
        `EFB textura RGB565, mitad izquierda = (255, 0, 255): ${rgb(r.texLeft)}`);
  check(near(rgb(r.texRight), [0, 255, 255]),
        `EFB textura RGB565, mitad derecha = (0, 255, 255): ${rgb(r.texRight)}`);
  check(near(rgb(r.texC8), [255, 255, 0]),
        `EFB textura C8 con TLUT RGB565 en TMEM = entrada 5 (255, 255, 0): ${rgb(r.texC8)}`);
}
if (failures) console.log('--- últimas líneas ---\n' + [...lines.slice(-25), ...consoleLines.slice(-15)].join('\n'));
await browser.close();
server.kill();
console.log(failures ? `${failures} fallos` : 'todas las comprobaciones pasaron');
process.exit(failures ? 1 : 0);

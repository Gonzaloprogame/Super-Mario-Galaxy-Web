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
check(!!r && !r.error, `Aurora devolvió la lectura del XFB y del EFB (${r && r.error ? r.error : 'sí'})`);
if (r && !r.error) {
  const x = r.xfb;
  const e = r.efb;
  check(r.frames > 0, `fotogramas GX dibujados por Aurora: ${r.frames}`);
  // The frame, as the display copy (GXCopyDisp) left it in the XFB
  check(near(rgb(x.corner), [24, 32, 96]), `XFB esquina = color de borrado (24, 32, 96): ${rgb(x.corner)}`);
  check(near(rgb(x.outside), [24, 32, 96]), `XFB fuera del triángulo = color de borrado: ${rgb(x.outside)}`);
  check(near(rgb(x.centre), [240, 176, 16]), `XFB centro del triángulo = color de vértice (240, 176, 16): ${rgb(x.centre)}`);
  check(near(rgb(x.indexed), [32, 200, 64]),
        `XFB triángulo de arrays indexados (direcciones físicas, big-endian) = (32, 200, 64): ${rgb(x.indexed)}`);
  check(near(rgb(x.displayList), [200, 48, 160]),
        `XFB triángulo de la lista de dibujo (GXCallDisplayList) = (200, 48, 160): ${rgb(x.displayList)}`);
  check(near(rgb(x.texLeft), [255, 0, 255]), `XFB textura RGB565, mitad izquierda = (255, 0, 255): ${rgb(x.texLeft)}`);
  check(near(rgb(x.texRight), [0, 255, 255]), `XFB textura RGB565, mitad derecha = (0, 255, 255): ${rgb(x.texRight)}`);
  check(near(rgb(x.texC8), [255, 255, 0]),
        `XFB textura C8 con TLUT RGB565 en TMEM = entrada 5 (255, 255, 0): ${rgb(x.texC8)}`);
  check(near(rgb(x.copyTop), [24, 32, 96]),
        `XFB copia del EFB, mitad superior = fondo copiado (24, 32, 96): ${rgb(x.copyTop)}`);
  check(near(rgb(x.copyLeft), [255, 0, 255]),
        `XFB copia del EFB, abajo a la izquierda = magenta copiado (255, 0, 255): ${rgb(x.copyLeft)}`);
  check(near(rgb(x.copyRight), [0, 255, 255]),
        `XFB copia del EFB, abajo a la derecha = cian copiado (0, 255, 255): ${rgb(x.copyRight)}`);
  // The EFB after the display copy's clear, with the marker drawn after it
  check(near(rgb(e.corner), [24, 32, 96]), `EFB tras la copia: esquina = color de borrado: ${rgb(e.corner)}`);
  check(near(rgb(e.centre), [24, 32, 96]),
        `EFB tras la copia: donde estaba el triángulo = color de borrado (la copia borró el EFB): ${rgb(e.centre)}`);
  check(near(rgb(e.marker), [250, 120, 0]),
        `EFB tras la copia: marcador dibujado después = (250, 120, 0): ${rgb(e.marker)}`);
  check(near(rgb(x.marker), [24, 32, 96]),
        `XFB donde está el marcador = color de borrado (se dibujó después de la copia): ${rgb(x.marker)}`);
}
if (failures) console.log('--- últimas líneas ---\n' + [...lines.slice(-25), ...consoleLines.slice(-15)].join('\n'));
await browser.close();
server.kill();
console.log(failures ? `${failures} fallos` : 'todas las comprobaciones pasaron');
process.exit(failures ? 1 : 0);

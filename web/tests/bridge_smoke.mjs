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
  // The scene's own EFB peeks (GXPeekARGB, GXPeekZ), answered from captures at their stream positions
  const g = r.game;
  check(near(rgb(g.tokenColour), [240, 176, 16]),
        `GXPeekARGB en la callback del token = triángulo (240, 176, 16): ${rgb(g.tokenColour)}`);
  check(Math.abs(g.tokenDepth - 0x800000) <= 0x100,
        `GXPeekZ en la callback del token = profundidad del triángulo (~0x800000): 0x${g.tokenDepth.toString(16)}`);
  check(near(rgb(g.afterCopyColour), [24, 32, 96]),
        `GXPeekARGB tras la copia a pantalla = color de borrado: ${rgb(g.afterCopyColour)}`);
  check(g.afterCopyDepth === 0xFFFFFF,
        `GXPeekZ tras la copia a pantalla = profundidad borrada (0xffffff): 0x${g.afterCopyDepth.toString(16)}`);
  check(near(rgb(g.afterCopyMarker), [250, 120, 0]),
        `GXPeekARGB del marcador = (250, 120, 0): ${rgb(g.afterCopyMarker)}`);
  check(near(rgb(x.marker), [24, 32, 96]),
        `XFB donde está el marcador = color de borrado (se dibujó después de la copia): ${rgb(x.marker)}`);
}
// What the page shows: the canvas web/shell/present.js paints with the XFB the scene's VI selects (vi_frame)
let shown = null;
const presentDeadline = Date.now() + 60000;
while (Date.now() < presentDeadline) {
  shown = await page.evaluate(() => globalThis.smgPresenter && { ...globalThis.smgPresenter });
  if (shown && shown.frames >= 3) break;
  await new Promise((resolve) => setTimeout(resolve, 250));
}
check(!!shown && shown.frames >= 3, `la página pinta el XFB que muestra el VI: ${shown ? shown.frames : 0} fotogramas`);
check(!!shown && shown.width === 640 && shown.height === 480, `tamaño del fotograma pintado: ${shown?.width}x${shown?.height}`);
if (shown && shown.frames >= 3) {
  const expected = [
    ['esquina', [16, 16], [24, 32, 96]], ['triángulo', [320, 272], [240, 176, 16]], ['fondo', [96, 408], [24, 32, 96]],
    ['arrays indexados', [568, 444], [32, 200, 64]], ['lista de dibujo', [568, 36], [200, 48, 160]],
    ['textura RGB565 izquierda', [44, 240], [255, 0, 255]], ['textura RGB565 derecha', [100, 240], [0, 255, 255]],
    ['textura C8', [568, 240], [255, 255, 0]], ['copia del EFB arriba', [272, 405], [24, 32, 96]],
    ['copia del EFB abajo izquierda', [272, 447], [255, 0, 255]], ['copia del EFB abajo derecha', [368, 447], [0, 255, 255]],
    ['marcador (fuera del XFB)', [320, 48], [24, 32, 96]],
  ];
  const pixels = await page.evaluate((points) => {
    const context = document.getElementById('screen').getContext('2d');
    return points.map(([x, y]) => Array.from(context.getImageData(x, y, 1, 1).data.slice(0, 3)));
  }, expected.map((e) => e[1]));
  expected.forEach(([name, , want], i) => check(near(pixels[i], want), `canvas de la página, ${name} = (${want}): ${pixels[i]}`));
}
// SMG_SCREENSHOT=<file.png>: the page's canvas as the presenter painted it
if (process.env.SMG_SCREENSHOT && shown && shown.frames > 0) {
  await page.locator('#screen').screenshot({ path: process.env.SMG_SCREENSHOT });
  console.log(`captura del canvas: ${process.env.SMG_SCREENSHOT}`);
}
if (failures) console.log('--- últimas líneas ---\n' + [...lines.slice(-25), ...consoleLines.slice(-15)].join('\n'));
await browser.close();
server.kill();
console.log(failures ? `${failures} fallos` : 'todas las comprobaciones pasaron');
process.exit(failures ? 1 : 0);

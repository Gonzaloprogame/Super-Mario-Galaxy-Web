// Prueba del renderizador web (Hito 5): la demo GX de Aurora (web/aurora/demo) en Chromium con WebGPU. Comprueba que
// Aurora crea el dispositivo en el WebGPU del navegador, dibuja fotogramas y que la escena GX es la esperada: el color
// de borrado en una esquina y en el fondo, el color del triángulo en su centro.
//   node web/tests/aurora_smoke.mjs <directorio con gx_demo.html/.js/.wasm>
// Por defecto el EFB se dibuja fuera de pantalla (?headless=1) y se lee desde el propio wasm con la API de lectura de
// Aurora (la de GXPeekARGB): Chromium headless con WebGPU por software (SwiftShader, --enable-unsafe-webgpu) pierde
// cualquier dispositivo que presente en un canvas (docs/BACKEND_WEBGPU.md). Con SMG_CANVAS=1 (equipo con GPU) se
// comprueba además la imagen del canvas.
import { createRequire } from 'node:module';
import http from 'node:http';
import { readFileSync, existsSync } from 'node:fs';
import { join } from 'node:path';

const require = createRequire(import.meta.url);
const { chromium } = require('playwright');
const dir = process.argv[2];
const types = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm' };
const server = http.createServer((req, res) => {
  const route = req.url.split('?')[0];
  const name = route === '/' ? 'gx_demo.html' : route.slice(1);
  const path = join(dir, name);
  if (!existsSync(path)) { res.writeHead(404); res.end(); return; }
  res.writeHead(200, { 'Content-Type': types[name.slice(name.lastIndexOf('.'))] || 'application/octet-stream' });
  res.end(readFileSync(path));
});
await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));

let failures = 0;
const check = (ok, what) => { console.log(`${ok ? 'PASS' : 'FAIL'} ${what}`); if (!ok) failures++; };

const browser = await chromium.launch({ args: ['--enable-unsafe-webgpu', ...(process.env.SMG_BROWSER_LOG === '1' ? ['--enable-logging=stderr', '--v=0'] : [])] });
const page = await browser.newPage();
const logs = [];
const verbose = process.env.SMG_VERBOSE === '1';
const record = (line) => { logs.push(line); if (verbose) console.log(line); };
page.on('console', (m) => record(`[${m.type()}] ${m.text()}`));
page.on('pageerror', (e) => record(`[pageerror] ${e}`));
// A page whose main thread never returns to the browser cannot answer page.evaluate: bound every call, so a stuck
// renderer is reported as one instead of hanging the test.
const evaluate = (fn, arg, ms = 15000) => Promise.race([
  page.evaluate(fn, arg),
  new Promise((_, reject) => setTimeout(() => reject(new Error(`la página no respondió en ${ms} ms`)), ms)),
]);
if (process.env.SMG_TRACE_DESTROY === '1') {
  // Debugging: who destroys the WebGPU device (the JS stack names the wasm functions in a --profiling-funcs build).
  if (process.env.SMG_HOLD_DEVICE === '1') await page.addInitScript(() => { globalThis.SMG_HOLD_DEVICE = true; });
  await page.addInitScript(() => {
    const original = GPUDevice.prototype.destroy;
    GPUDevice.prototype.destroy = function () {
      console.log('GPUDevice.destroy() from:\n' + new Error().stack);
      return original.call(this);
    };
    const requestDevice = GPUAdapter.prototype.requestDevice;
    GPUAdapter.prototype.requestDevice = async function (...args) {
      const device = await requestDevice.apply(this, args);
      device.lost.then((info) => console.log(`GPUDevice.lost (browser): reason=${info.reason} message=${info.message}`));
      if (globalThis.SMG_HOLD_DEVICE) (globalThis.__heldDevices ||= []).push(device);
      device.addEventListener('uncapturederror', (e) => console.log(`GPUDevice uncapturederror: ${e.error.message}`));
      return device;
    };
  });
}
if (process.env.SMG_DUMP_SHADERS) {
  // Debugging: every WGSL module the page creates, written to the given directory.
  const { mkdirSync, writeFileSync } = await import('node:fs');
  mkdirSync(process.env.SMG_DUMP_SHADERS, { recursive: true });
  let count = 0;
  await page.exposeFunction('__smgDumpShader', (label, code) => {
    writeFileSync(join(process.env.SMG_DUMP_SHADERS, `${String(count++).padStart(3, '0')}-${(label || 'shader').replace(/[^a-z0-9_-]/gi, '_')}.wgsl`), code);
  });
  await page.addInitScript(() => {
    const create = GPUDevice.prototype.createShaderModule;
    GPUDevice.prototype.createShaderModule = function (desc) {
      globalThis.__smgDumpShader(desc.label, desc.code);
      return create.call(this, desc);
    };
  });
}
const canvasMode = process.env.SMG_CANVAS === '1';
const query = new URLSearchParams(canvasMode ? {} : { headless: '1' });
if (process.env.SMG_TEV) query.set('tev', process.env.SMG_TEV);
await page.goto(`http://127.0.0.1:${server.address().port}/?${query}`);

let state = null;
let stuck = null;
const deadline = Date.now() + 120000;
while (Date.now() < deadline) {
  try {
    state = await evaluate(() => globalThis.gxDemo || null, undefined, 10000);
  } catch (e) {
    stuck = String(e.message);
    break;
  }
  if (state && (state.error || (state.frames >= 30 && (canvasMode || state.peeks)))) break;
  await new Promise((r) => setTimeout(r, 500));
}
check(!stuck, `la página sigue respondiendo (${stuck || 'sí'})`);
let adapter = 'desconocido';
if (!stuck) {
  adapter = await evaluate(async () => {
    const a = await navigator.gpu?.requestAdapter();
    return a ? `${a.info.vendor}/${a.info.architecture}` : 'none';
  });
}
console.log(`adaptador WebGPU: ${adapter}; estado de la demo: ${JSON.stringify(state)}`);
check(state && !state.error && state.frames >= 30, 'Aurora dibuja al menos 30 fotogramas GX sin error');
const near = (got, want, tol = 6) => want.every((v, i) => Math.abs(got[i] - v) <= tol);
if (!canvasMode) {
  // GXPeekARGB's u32: 0xAARRGGBB.
  const rgb = (argb) => (argb === undefined ? [] : [(argb >>> 16) & 255, (argb >>> 8) & 255, argb & 255]);
  const peeks = state?.peeks || {};
  console.log('lecturas del EFB:', JSON.stringify(peeks, (k, v) => (typeof v === 'number' ? '0x' + v.toString(16) : v)));
  check(state?.headless === true, 'el EFB se dibuja fuera de pantalla (superficie headless de Aurora)');
  check(near(rgb(peeks.corner), [24, 32, 96]), `EFB esquina = color de borrado GX (24, 32, 96): ${rgb(peeks.corner)}`);
  check(near(rgb(peeks.outside), [24, 32, 96]), `EFB fuera del triángulo = color de borrado: ${rgb(peeks.outside)}`);
  check(near(rgb(peeks.centre), [240, 176, 16]), `EFB centro del triángulo = color de vértice (240, 176, 16): ${rgb(peeks.centre)}`);
  if (failures) console.log('--- consola de la página ---\n' + logs.slice(-40).join('\n'));
  await browser.close();
  server.close();
  console.log(failures ? `${failures} fallos` : 'todas las comprobaciones pasaron');
  process.exit(failures ? 1 : 0);
}

// The canvas as the page shows it (a screenshot), read back in the page: one pixel near a corner, one at the
// triangle's centroid (x 0, y about -0.13 in GX's [-1, 1] coordinates, so 50 % across and 56.7 % down).
if (stuck) {
  console.log('--- consola de la página ---\n' + logs.slice(-40).join('\n'));
  console.log(`${failures} fallos`);
  process.exit(1);
}
const png = await page.locator('#canvas').screenshot({ timeout: 15000 });
const pixels = await evaluate(async (b64) => {
  const img = await createImageBitmap(await (await fetch(`data:image/png;base64,${b64}`)).blob());
  const c = new OffscreenCanvas(img.width, img.height);
  const ctx = c.getContext('2d');
  ctx.drawImage(img, 0, 0);
  const at = (fx, fy) => Array.from(ctx.getImageData(Math.floor(img.width * fx), Math.floor(img.height * fy), 1, 1).data);
  return { size: [img.width, img.height], corner: at(0.03, 0.03), centre: at(0.5, 0.567), outside: at(0.15, 0.85) };
}, png.toString('base64'));
console.log('píxeles:', JSON.stringify(pixels));
check(near(pixels.corner, [24, 32, 96]), `esquina = color de borrado GX (24, 32, 96): ${pixels.corner.slice(0, 3)}`);
check(near(pixels.outside, [24, 32, 96]), `fuera del triángulo = color de borrado: ${pixels.outside.slice(0, 3)}`);
check(near(pixels.centre, [240, 176, 16]), `centro del triángulo = color de vértice (240, 176, 16): ${pixels.centre.slice(0, 3)}`);

if (failures) console.log('--- consola de la página ---\n' + logs.slice(-40).join('\n'));
await browser.close();
server.close();
console.log(failures ? `${failures} fallos` : 'todas las comprobaciones pasaron');
process.exit(failures ? 1 : 0);

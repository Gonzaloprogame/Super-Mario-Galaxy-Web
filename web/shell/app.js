// The page: checks what the engine needs from the browser, takes the user's copy of the game (an RVZ disc image or
// game.pak) and runs the engine in a worker.
// It shows exactly what the engine reports; it never shows the game as running when the engine has not started.
'use strict';

const $ = (id) => document.getElementById(id);
const statusEl = $('status');
const logEl = $('log');

function setStatus(text, kind = 'info') {
  statusEl.textContent = text;
  statusEl.dataset.kind = kind;
}

function log(text) {
  logEl.textContent += text + '\n';
  logEl.scrollTop = logEl.scrollHeight;
}

// What the engine cannot run without, checked before anything is loaded.
function requirements() {
  const missing = [];
  if (typeof WebAssembly !== 'object') missing.push('WebAssembly');
  if (typeof SharedArrayBuffer !== 'function' || !self.crossOriginIsolated) {
    missing.push('SharedArrayBuffer con aislamiento de origen cruzado (el servidor debe enviar las cabeceras ' +
                 'Cross-Origin-Opener-Policy: same-origin y Cross-Origin-Embedder-Policy: require-corp)');
  }
  if (typeof Worker !== 'function') missing.push('Web Workers');
  return missing;
}

// The graphics mode also needs WebGPU (the engine's render thread uses it from a worker).
const hasWebGPU = typeof navigator === 'object' && 'gpu' in navigator;

const engines = {
  graphics: { script: 'smg_web.js', label: 'con gráficos: Aurora sobre WebGPU' },
  diagnostic: { script: 'smg_headless_web.js', label: 'modo diagnóstico, sin renderizador' },
};

// What the screen shows comes only from frames the engine posted (web/shell/present.js): nothing is drawn here.
const presenter = smgAttachPresenter($('screen'));
setInterval(() => {
  if (presenter.frames > 0) {
    $('screen-info').textContent = `Fotogramas mostrados: ${presenter.frames} (${presenter.width}×${presenter.height}), ` +
      'el XFB que el VI del juego tiene seleccionado.';
  }
}, 500);

// What the chosen file is, from its first bytes (the extension does not matter): the same checks the engine makes
// (web/host/headless_host.c, web/host/rvz_reader.c), shown at once, before anything is loaded.
const be32 = (bytes, at) => ((bytes[at] << 24) | (bytes[at + 1] << 16) | (bytes[at + 2] << 8) | bytes[at + 3]) >>> 0;
const ascii = (bytes, at, n) => String.fromCharCode(...bytes.slice(at, at + n)).replace(/[^\x20-\x7e]/g, '?');

async function inspect(file) {
  const bytes = new Uint8Array(await file.slice(0, 0x440).arrayBuffer());
  const size = `${(file.size / 2 ** 30).toFixed(2)} GB`;
  const magic = ascii(bytes, 0, 4);
  // ascii() shows the signatures' last byte, 0x01, as '?'
  if (bytes.length >= 0x60 && magic === 'RVZ?') {
    const id = ascii(bytes, 0x58, 6);
    const compression = be32(bytes, 0x4c);
    const names = { 0: 'sin compresión', 2: 'bzip2', 3: 'LZMA', 4: 'LZMA2', 5: 'Zstandard' };
    if (compression !== 0 && compression !== 5) {
      return { ok: false, text: `Imagen RVZ de ${id} (${size}) comprimida con ${names[compression] || 'un método desconocido'}: ` +
        'el motor lee Zstandard (la de Dolphin por defecto) o sin compresión. Conviértela de nuevo en Dolphin con Zstandard.' };
    }
    if (id !== 'RMGE01') {
      return { ok: false, text: `Imagen RVZ de ${id} (${size}): esta versión del motor es para Super Mario Galaxy de ` +
        'Norteamérica (RMGE01).' };
    }
    return { ok: true, text: `Imagen RVZ de ${id} (${size}, ${names[compression]}): la página leerá de ella la partición ` +
      'del juego. Pulsa «Arrancar el motor».' };
  }
  if (magic === 'WIA?') {
    return { ok: false, text: `Imagen WIA (${size}): el motor lee RVZ. Conviértela a RVZ en Dolphin.` };
  }
  if (magic === 'WBFS') {
    return { ok: false, text: `Imagen WBFS (${size}): guarda la partición del juego cifrada. Conviértela a RVZ en Dolphin.` };
  }
  if (bytes.length >= 0x424 && be32(bytes, 0x18) === 0x5d1c9ea3 && be32(bytes, 0x420) === 0) {
    // A disc's own header, which (unlike game.pak's boot.bin) does not point to a main.dol
    return { ok: false, text: `Imagen de disco ISO de ${ascii(bytes, 0, 6)} (${size}): guarda la partición del juego ` +
      'cifrada. Conviértela a RVZ en Dolphin.' };
  }
  // Anything else is read as game.pak, as the engine does: boot.bin starts with the game ID
  const id = ascii(bytes, 0, 6);
  if (/^[A-Z0-9]{6}$/.test(id)) {
    return { ok: id === 'RMGE01', text: id === 'RMGE01'
      ? `game.pak de ${id} (${size}). Pulsa «Arrancar el motor».`
      : `game.pak de ${id} (${size}): esta versión del motor es para Super Mario Galaxy de Norteamérica (RMGE01).` };
  }
  const hex = Array.from(bytes.slice(0, 8), (b) => b.toString(16).padStart(2, '0')).join(' ');
  return { ok: false, text: `«${file.name}» (${size}) no es una imagen RVZ ni un game.pak: empieza por ${hex} ` +
    `(«${ascii(bytes, 0, 8)}»). Un RVZ de Dolphin empieza por «RVZ».` };
}

$('pak').addEventListener('change', async () => {
  const file = $('pak').files[0];
  $('file-info').textContent = '';
  if (!file) {
    return;
  }
  $('file-info').textContent = `Analizando ${file.name}…`;
  try {
    const result = await inspect(file);
    $('file-info').textContent = result.text;
    $('file-info').dataset.ok = String(result.ok);
  } catch (error) {
    $('file-info').textContent = `No se pudo leer ${file.name}: ${error}`;
    $('file-info').dataset.ok = 'false';
  }
});

$('copy-log').addEventListener('click', async () => {
  const text = `${statusEl.textContent}\n${$('file-info').textContent}\n\n${logEl.textContent}`;
  try {
    await navigator.clipboard.writeText(text);
    $('copy-log').textContent = 'Copiado';
  } catch {
    // Without clipboard access: select it, for the user to copy
    const range = document.createRange();
    range.selectNodeContents(logEl);
    getSelection().removeAllRanges();
    getSelection().addRange(range);
    $('copy-log').textContent = 'Seleccionado: cópialo';
  }
  setTimeout(() => { $('copy-log').textContent = 'Copiar el registro'; }, 2000);
});

function start() {
  const file = $('pak').files[0];
  if (!file) {
    setStatus('Selecciona primero tu copia del juego (.rvz o game.pak).', 'error');
    return;
  }
  const mode = $('mode').value;
  if (mode === 'graphics' && !hasWebGPU) {
    setStatus('Este navegador no ofrece WebGPU: usa el modo diagnóstico o un navegador con WebGPU.', 'error');
    return;
  }
  const engine = engines[mode];
  const seconds = Math.max(0, Number($('seconds').value) || 0);
  if (mode === 'diagnostic' && seconds === 0) {
    setStatus('El modo diagnóstico necesita una duración en segundos.', 'error');
    return;
  }
  $('run').disabled = true;
  logEl.textContent = '';
  $('screen-card').hidden = mode !== 'graphics';
  setStatus(`Cargando ${file.name} (${(file.size / 2 ** 20).toFixed(1)} MB)…`);
  const worker = new Worker(engine.script);
  document.body.dataset.state = 'running';
  worker.onmessage = ({ data }) => {
    switch (data.type) {
      case 'log':
        log(data.text);
        break;
      case 'status':
        setStatus(data.text);
        break;
      case 'error':
        setStatus(data.text, 'error');
        document.body.dataset.state = 'error';
        $('run').disabled = false;
        break;
      case 'exit':
        if (data.code === 0) {
          setStatus('El motor terminó la ejecución (código 0). Consulta el informe en el registro.', 'ok');
          document.body.dataset.state = 'done';
        } else {
          setStatus(`El motor se detuvo con el código ${data.code}. El motivo aparece en el registro.`, 'error');
          document.body.dataset.state = 'error';
        }
        $('run').disabled = false;
        worker.terminate();
        break;
    }
  };
  worker.onerror = (event) => {
    setStatus(`Error en el worker: ${event.message}`, 'error');
    document.body.dataset.state = 'error';
    $('run').disabled = false;
  };
  worker.postMessage({ file, seconds, label: engine.label });
}

const missing = requirements();
if (missing.length) {
  setStatus('Este navegador o servidor no permite ejecutar el motor. Falta: ' + missing.join('; '), 'error');
  document.body.dataset.state = 'unsupported';
  $('run').disabled = true;
} else {
  setStatus('Listo. Selecciona tu copia del juego (.rvz o game.pak) para arrancar el motor.');
  document.body.dataset.state = 'ready';
}
$('run').addEventListener('click', start);

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

// The console's language, as the browser's (the host keeps it only if the disc has it: web/host/headless_host.c)
{
  const wanted = { es: '4', en: '1', fr: '3', de: '2', it: '5', nl: '6', ja: '0' }[(navigator.language || '').slice(0, 2)];
  if (wanted) {
    $('language').value = wanted;
  }
}

// The NAND (the console's storage: the game's saves) kept between sessions, in IndexedDB by the disc's ID: the engine
// hands it over after each change (presenter.onNand) and gets the last copy when it starts (web/host/headless_host.c).
// Without IndexedDB (a private window may refuse it) the game still saves, until the page is closed.
const nandStore = {
  open() {
    return new Promise((resolve, reject) => {
      const request = indexedDB.open('smg-web', 1);
      request.onupgradeneeded = () => request.result.createObjectStore('nand');
      request.onsuccess = () => resolve(request.result);
      request.onerror = () => reject(request.error);
    });
  },
  async run(mode, action) {
    const db = await this.open();
    return new Promise((resolve, reject) => {
      const request = action(db.transaction('nand', mode).objectStore('nand'));
      request.onsuccess = () => resolve(request.result);
      request.onerror = () => reject(request.error);
    });
  },
  async get(id) {
    try {
      return (await this.run('readonly', (store) => store.get(id))) || null;
    } catch (error) {
      log(`[página] no se pudo leer la partida guardada de este navegador: ${error}`);
      return null;
    }
  },
  async put(id, data) {
    try {
      await this.run('readwrite', (store) => store.put(data, id));
    } catch (error) {
      log(`[página] no se pudo guardar la partida en este navegador (se perderá al cerrar la página): ${error}`);
    }
  },
};

// The controls (controls.js): written into the engine's memory once the engine gives it
const controls = smgAttachControls($('pad'), $('screen'));

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
    return { ok: true, id, text: `Imagen RVZ de ${id} (${size}, ${names[compression]}): la página leerá de ella la partición ` +
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
    return { ok: id === 'RMGE01', id, text: id === 'RMGE01'
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

async function start() {
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
  // The renderer stopping on an error is the engine stopping: shown as such, then the worker ends (after the trace
  // the renderer prints has reached the log)
  let rendererFailed = false;
  presenter.onFatal = (text) => {
    rendererFailed = true;
    setStatus(`El renderizador (Aurora) se detuvo por un error: ${text}. El registro incluye los datos para ` +
              'diagnosticarlo: cópialo con «Copiar el registro».', 'error');
    document.body.dataset.state = 'error';
    presenter.onFatal = null;
    setTimeout(() => {
      worker.terminate();
      $('run').disabled = false;
    }, 3000);
  };
  // The engine's end, as Emscripten reports it (the worker's 'exit') or as the host announces it on the presenter's
  // channel, whichever comes first: the host's announcement reaches the page even when Emscripten's exit does not end
  // the runtime. The worker ends here, after the log's last lines have come in.
  let ended = false;
  const finish = (code) => {
    if (ended) {
      return;
    }
    ended = true;
    presenter.onExit = null;
    controls.detach();
    if (!rendererFailed) { // otherwise the renderer's error stays the status: it is why the engine stopped
      if (code === 0) {
        setStatus('El motor terminó la ejecución (código 0). Consulta el informe en el registro.', 'ok');
        document.body.dataset.state = 'done';
      } else {
        setStatus(`El motor se detuvo con el código ${code}. El motivo aparece en el registro.`, 'error');
        document.body.dataset.state = 'error';
      }
    }
    setTimeout(() => {
      worker.terminate();
      $('run').disabled = false;
    }, 1500);
  };
  presenter.onExit = finish;
  worker.onmessage = ({ data }) => {
    switch (data.type) {
      case 'log':
        log(data.text);
        break;
      case 'input':
        // The engine's shared memory and where its control words are (web/host/console_services.h)
        controls.attach(new Int32Array(data.buffer, data.address, data.words));
        break;
      case 'status':
        if (!rendererFailed) {
          setStatus(data.text); // after a renderer failure, its error stays the status
        }
        break;
      case 'error':
        setStatus(data.text, 'error');
        document.body.dataset.state = 'error';
        $('run').disabled = false;
        break;
      case 'exit':
        finish(data.code);
        break;
    }
  };
  // An abort in one of the engine's threads (its render thread among them): the error reaches the page as an uncaught
  // error, not as the engine's exit. It is the engine stopping: shown, with its stack (wasm function indices, which
  // tools/wasm_symbols.py names with the build's smg_web.js.symbols), and the worker ends.
  const engineFailed = (text, stack) => {
    if (ended) {
      return;
    }
    ended = true;
    log(`[página] el motor se detuvo por un error: ${text}`);
    if (stack) {
      log(`[página] pila del error:\n${stack}`);
    }
    setStatus(`El motor se detuvo por un error: ${text}. Copia el registro con «Copiar el registro».`, 'error');
    document.body.dataset.state = 'error';
    presenter.onExit = null;
    controls.detach();
    setTimeout(() => {
      worker.terminate();
      $('run').disabled = false;
    }, 1500);
  };
  worker.onerror = (event) => {
    event.preventDefault();
    engineFailed(event.message || 'error en el worker', event.error && event.error.stack);
  };
  window.onerror = (message, source, line, column, error) => {
    if (document.body.dataset.state === 'running' && /Aborted|RuntimeError|wasm/.test(String(message))) {
      engineFailed(String(message), error && error.stack);
      return true;
    }
    return false;
  };
  // The saves this browser kept for this disc
  const disc = await inspect(file).catch(() => ({}));
  const nand = disc.id ? await nandStore.get(disc.id) : null;
  if (nand) {
    log(`[página] partida guardada de ${disc.id} en este navegador: ${nand.length} bytes`);
  }
  presenter.onNand = (id, data) => nandStore.put(id, data);
  worker.postMessage({ file, seconds, language: Number($('language').value), label: engine.label, nand });
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

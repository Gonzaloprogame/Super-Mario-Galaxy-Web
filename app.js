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
  loading.line(text);
}

// The loading screen (web/shell/index.html, #loading) until the game starts. Each step changes when the engine says so
// (its status, its log, the renderer's progress in shared memory): nothing advances on a timer. The shaders' bar is
// the count of pipelines the render thread has built from the list kept by earlier sessions (Aurora patch 0026).
// globalThis.smgLoading: the bar's last reading and the steps' final texts, for the tests.
const loading = {
  el: null, progress: null, frame: 0, pendingKey: '', withRenderer: false, done: true,
  step(id, state, text) {
    const item = document.getElementById(id);
    if (!item) return;
    item.dataset.state = state;
    if (text !== undefined) item.textContent = text;
  },
  note(text) {
    const note = document.getElementById('loading-note');
    if (note) note.textContent = text;
  },
  bar(fraction) {
    const bar = document.getElementById('shader-bar');
    if (bar) bar.style.width = `${Math.round(fraction * 100)}%`;
  },
  start(label, hasList, pendingKey) {
    this.el = document.getElementById('loading');
    if (!this.el) return;
    this.done = false;
    this.pendingKey = pendingKey;
    this.withRenderer = /Aurora|gráficos/i.test(label);
    this.el.hidden = false;
    this.step('step-engine', 'doing', 'Cargando el motor');
    this.step('step-disc', 'wait', 'Abriendo el disco');
    this.step('step-renderer', this.withRenderer ? 'wait' : 'skip',
              this.withRenderer ? 'Iniciando el renderizador' : 'Sin renderizador (modo diagnóstico)');
    this.step('step-shaders', this.withRenderer ? 'wait' : 'skip',
              this.withRenderer ? 'Preparando shaders guardados' : 'Sin shaders (modo diagnóstico)');
    this.bar(0);
    this.note(hasList || !this.withRenderer ? '' :
              'Primera vez en este navegador: no hay shaders guardados. Se guardan mientras juegas y la próxima vez ' +
              'se preparan aquí, antes de empezar.');
    globalThis.smgLoading = { steps: {}, shaders: null, shown: true };
  },
  line(text) {
    if (this.done) return;
    // The host has read the disc's partition (web/host/headless_host.c)
    if (/^\[host\] .*: RMGE01, \d+ bytes, main\.dol/.test(text)) {
      this.step('step-engine', 'done');
      this.step('step-disc', 'done');
      if (this.withRenderer) this.step('step-renderer', 'doing');
    }
    if (/^\[host\] running the game/.test(text)) this.finish();
  },
  status(text) {
    if (this.done) return;
    if (/^Motor en marcha/.test(text)) {
      this.step('step-engine', 'done');
      this.step('step-disc', 'doing');
    }
  },
  attach(buffer, address) {
    // {phase 0 none / 1 building / 2 done, built, total, rejected} (web/host/aurora_bridge.cpp, g_preload)
    this.progress = new Uint32Array(buffer, address, 4);
    this.step('step-renderer', 'done');
    this.step('step-shaders', 'doing');
    const tick = () => {
      if (this.done || !this.progress) return;
      this.update();
      this.frame = requestAnimationFrame(tick);
    };
    this.frame = requestAnimationFrame(tick);
  },
  // One reading of the render thread's count (also the last one, as the game starts)
  update() {
    if (!this.progress) return;
    {
      const phase = Atomics.load(this.progress, 0), built = Atomics.load(this.progress, 1);
      const total = Atomics.load(this.progress, 2), rejected = Atomics.load(this.progress, 3);
      globalThis.smgLoading.shaders = { phase, built, total, rejected };
      if (phase === 1) {
        // A start whose preload never ends leaves this mark: the next start drops the list
        try { if (this.pendingKey) localStorage.setItem(this.pendingKey, '1'); } catch { /* no storage */ }
        this.step('step-shaders', 'doing', `Preparando shaders guardados: ${built} de ${total}`);
        this.bar(total ? built / total : 0);
      } else if (phase === 2) {
        try { if (this.pendingKey) localStorage.removeItem(this.pendingKey); } catch { /* no storage */ }
        this.step('step-shaders', 'done', total ? `Shaders preparados: ${total}` + (rejected ? ` (${rejected} descartados)` : '')
                                               : 'Sin shaders guardados todavía');
        this.bar(1);
      }
    }
  },
  finish() {
    if (this.done) return;
    this.update();
    this.done = true;
    cancelAnimationFrame(this.frame);
    for (const id of ['step-engine', 'step-disc', 'step-renderer', 'step-shaders']) {
      const item = document.getElementById(id);
      if (item && item.dataset.state !== 'skip') item.dataset.state = 'done';
      if (globalThis.smgLoading) globalThis.smgLoading.steps[id] = item ? item.textContent : '';
    }
    if (globalThis.smgLoading) globalThis.smgLoading.shown = false;
    if (this.el) this.el.hidden = true;
  },
  stop() {
    // The engine stopped before the game started: the screen goes, the status says why
    this.done = true;
    cancelAnimationFrame(this.frame);
    if (globalThis.smgLoading) globalThis.smgLoading.shown = false;
    if (this.el) this.el.hidden = true;
  },
};

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
  db: null,
  open() {
    // One connection for the page's life (one per save piled up, and would block a version change)
    this.db ??= new Promise((resolve, reject) => {
      const request = indexedDB.open('smg-web', 1);
      request.onupgradeneeded = () => request.result.createObjectStore('nand');
      request.onsuccess = () => resolve(request.result);
      request.onerror = () => reject(request.error);
    });
    this.db.catch(() => { this.db = null; }); // a failed open is tried again next time
    return this.db;
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
// The frame-rate badge over the screen (web/shell/fps.js), shown or hidden with its button (remembered)
const fps = smgAttachFps($('fps'), presenter);
const showFpsButton = () => $('fps-toggle').setAttribute('aria-pressed', String(fps.visible));
showFpsButton();
$('fps-toggle').addEventListener('click', () => {
  fps.setVisible(!fps.visible);
  showFpsButton();
});
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

// The game's sound (web/shell/audio_worklet.js): its context is made at the click that starts the engine (browsers
// start sound only after a gesture), and the engine's ring of samples is handed to it when the worker says where it
// is. globalThis.smgAudio: {context, node, ring} for the tests.
const audio = { context: null, node: null, ring: null, ready: null };
globalThis.smgAudio = audio;

function startAudio() {
  if (audio.context !== null || typeof AudioContext === 'undefined') {
    return;
  }
  try {
    audio.context = new AudioContext({ latencyHint: 'interactive' });
  } catch (error) {
    log(`[página] sin sonido: ${error.message}`);
    return;
  }
  audio.ready = audio.context.audioWorklet.addModule('audio_worklet.js').then(() => {
    audio.node = new AudioWorkletNode(audio.context, 'smg-audio', { numberOfInputs: 0, outputChannelCount: [2] });
    audio.node.connect(audio.context.destination);
    applyVolume();
    if (audio.ring !== null) {
      audio.node.port.postMessage({ type: 'ring', ...audio.ring });
    }
  }).catch((error) => log(`[página] sin sonido: ${error.message}`));
  audio.context.resume().catch(() => {});
}

function attachAudio(buffer, address) {
  audio.ring = { buffer, address };
  if (audio.node !== null) {
    audio.node.port.postMessage({ type: 'ring', buffer, address });
  }
}

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
  if (mode === 'graphics') {
    startAudio();
  }
  logEl.textContent = '';
  $('screen-card').hidden = mode !== 'graphics';
  if (mode === 'graphics') {
    requestAnimationFrame(() => $('screen-card').scrollIntoView({ behavior: 'smooth', block: 'start' }));
  }
  setStatus(`Cargando ${file.name} (${(file.size / 2 ** 20).toFixed(1)} MB)…`);
  const worker = new Worker(engine.script);
  document.body.dataset.state = 'running';
  // The renderer stopping on an error is the engine stopping: shown as such, then the worker ends (after the trace
  // the renderer prints has reached the log)
  let rendererFailed = false;
  presenter.onFatal = (text, stack) => {
    rendererFailed = true;
    if (stack) {
      // An abort's C stack (web/platform/abort_report_wasm.c): tools/wasm_symbols.py names its functions
      log(`[página] ${text}; pila:\n${stack}`);
    }
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
  // Before the end: the host's last lines (its final report) are still in the watch log
  presenter.onExit = (code) => { readWatch(); finish(code); };
  // The watch log (web/host/headless_host.c, host_log): the host's reports come through a ring in the engine's shared
  // memory, read here every second, not through the engine's stderr: that goes through the game's thread, and when the
  // game hangs it holds the log back (the reports with it). The ring also holds the game's thread's heartbeat; when it
  // has not moved for 8 s, the ring's lines are marked "[vigía]" and the status says the game's thread does not answer.
  let watch = null;
  let watchSaid = false;
  const readWatch = () => {
    if (!watch) {
      return;
    }
    const written = Atomics.load(watch.words, 0);
    const beat = Atomics.load(watch.words, 2);
    const now = performance.now();
    if (beat !== watch.beat) {
      watch.beat = beat;
      watch.beatAt = now;
    }
    if (written === watch.read) {
      return;
    }
    const size = watch.text.length;
    const count = Math.min((written - watch.read) >>> 0, size);
    const bytes = new Uint8Array(count);
    for (let i = 0; i < count; i++) {
      bytes[i] = watch.text[(written - count + i) % size];
    }
    watch.read = written;
    const silent = beat !== 0 && now - watch.beatAt > 8000;
    if (silent && !watchSaid) {
      watchSaid = true;
      log(`[página] el hilo del juego lleva ${((now - watch.beatAt) / 1000).toFixed(0)} s sin llamar al host, pero el ` +
          'host sigue informando: el hilo del juego no responde. Sus informes siguen llegando (líneas «[vigía]»).');
      if (!rendererFailed) {
        setStatus('El hilo del juego no responde. Los informes del host siguen llegando (líneas «[vigía]» del ' +
                  'registro): cópialo con «Copiar el registro».', 'error');
      }
    }
    for (const line of new TextDecoder().decode(bytes).split('\n')) {
      if (line) {
        log(silent ? `[vigía] ${line}` : line);
      }
    }
  };
  const watchTimer = setInterval(() => {
    readWatch();
    if (ended) {
      clearInterval(watchTimer);
    }
  }, 1000);
  worker.onmessage = ({ data }) => {
    switch (data.type) {
      case 'log':
        log(data.text);
        break;
      case 'watch': {
        // The ring: {written, size, beat, reserved: u32; text[size]} (web/host/headless_host.c, sWatch)
        const words = new Uint32Array(data.buffer, data.address, 4);
        watch = { words, text: new Uint8Array(data.buffer, data.address + 16, words[1]), read: Atomics.load(words, 0),
                  beat: Atomics.load(words, 2), beatAt: performance.now() };
        break;
      }
      case 'input':
        // The engine's shared memory and where its control words are (web/host/console_services.h)
        controls.attach(new Int32Array(data.buffer, data.address, data.words));
        break;
      case 'present':
        // The frames' slots in the engine's shared memory (web/host/aurora_bridge.h): painted from there
        presenter.attachMemory(data.buffer, data.address);
        break;
      case 'audio':
        // The engine's ring of samples (web/host/audio_output.h), in its shared memory
        attachAudio(data.buffer, data.address);
        break;
      case 'preload':
        // Where the render thread counts the shaders it builds before the game (web/host/aurora_bridge.cpp)
        loading.attach(data.buffer, data.address);
        break;
      case 'pipelines':
        // The pipelines known now: kept for the next session's loading screen
        if (cacheKey && data.count !== keptShaders) {
          keptShaders = data.count;
          nandStore.put(cacheKey, data.data);
          log(`[página] shaders guardados para la próxima vez: ${data.count}`);
        }
        break;
      case 'renderdebug':
        // The renderer's diagnostic mask (Aurora patch 0017): smgRenderDebug[0] = mask, read at each frame's start
        globalThis.smgRenderDebug = new Int32Array(data.buffer, data.address, 1);
        break;
      case 'status':
        if (!rendererFailed) {
          setStatus(data.text); // after a renderer failure, its error stays the status
        }
        loading.status(data.text);
        break;
      case 'error':
        loading.stop();
        readWatch(); // the host's last lines, before the error
        if (data.stack) log(`[página] ${data.text}; pila:\n${data.stack}`);
        setStatus(data.text, 'error');
        document.body.dataset.state = 'error';
        $('run').disabled = false;
        break;
      case 'exit':
        loading.stop();
        readWatch(); // the host's last lines (its final report), before the end
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
    // The copy the session starts from, kept apart before the game can write over it: if the engine cannot read it
    // (a damaged copy, a format a later version changes), the next save would replace it with an empty NAND
    await nandStore.put(`${disc.id}.anterior`, nand);
  }
  presenter.onNand = (id, data) => nandStore.put(id, data);
  // The shaders (pipelines) this browser kept for this disc from earlier sessions (Aurora patch 0026): built before
  // the game starts, with the loading screen's bar. A start whose preload did not finish (the page closed, or a
  // pipeline the renderer cannot build) leaves its mark: then the list is dropped, so it can never block the start.
  const cacheKey = disc.id ? `${disc.id}.pipelines` : '';
  const pendingKey = disc.id ? `smg-preload-pending-${disc.id}` : '';
  let pipelines = cacheKey ? await nandStore.get(cacheKey) : null;
  let preloadInterrupted = false;
  try { preloadInterrupted = !!pendingKey && localStorage.getItem(pendingKey) === '1'; } catch { /* no storage */ }
  if (pipelines && preloadInterrupted) {
    log('[página] la precarga de shaders anterior no terminó: se descarta la lista guardada y se rehará jugando');
    pipelines = null;
    await nandStore.put(cacheKey, null);
  }
  try { if (pendingKey) localStorage.removeItem(pendingKey); } catch { /* no storage */ }
  if (pipelines) {
    log(`[página] shaders guardados de sesiones anteriores: ${pipelines.length} bytes`);
  }
  let keptShaders = 0;
  // ?debug=1: a second canvas with the game's largest copy of each frame, as Aurora made it (web/host/aurora_bridge.cpp)
  const debug = new URLSearchParams(location.search).get('debug') === '1';
  if (debug) {
    let debugCanvas = $('debug-screen');
    if (!debugCanvas) {
      debugCanvas = document.createElement('canvas');
      debugCanvas.id = 'debug-screen';
      debugCanvas.style.cssText = 'display:block;width:100%;max-width:640px;margin-top:8px;background:#000';
      $('screen').after(debugCanvas);
    }
    presenter.onDebug = ({ width, height, pixels }) => {
      debugCanvas.width = width;
      debugCanvas.height = height;
      debugCanvas.getContext('2d').putImageData(new ImageData(new Uint8ClampedArray(pixels), width, height), 0, 0);
    };
  }
  // ?hangtest=1 (web/tests/browser_smoke.mjs): the host hangs the game's thread on purpose, to test the watch log
  // (?hangtest=yield: the thread waits yielding instead, as Nebula's scheduler sleeping for work; ?hangtest=trap: the
  // host calls a function that does not exist, to test that the error's stack reaches the log; ?hangtest=nullwrite: it
  // writes through a null pointer, to test that the report says it)
  const hangtestParam = new URLSearchParams(location.search).get('hangtest');
  const hangtest = { 1: 'hangtest', yield: 'hangtest-yield', trap: 'hangtest-trap', nullwrite: 'hangtest-nullwrite' }[hangtestParam] || '';
  // ?tour=Stage:scenario,...&tourframes=N: the galaxy tour (web/host/headless_host.c), every N scene frames
  const tour = new URLSearchParams(location.search).get('tour') || '';
  const tourFrames = Number(new URLSearchParams(location.search).get('tourframes')) || 0;
  worker.postMessage({ file, seconds, language: Number($('language').value), label: engine.label, nand, debug, hangtest,
                       tour, tourFrames, pipelines });
  loading.start(engine.label, !!pipelines, pendingKey);
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

// The page's own controls: the volume (the AudioWorklet's gain), full screen for the screen and its controls, and
// the drop zone's highlight while a file is dragged over it
function applyVolume() {
  const value = Number($('volume').value) / 100;
  if (audio.node !== null) {
    audio.node.port.postMessage({ type: 'volume', value });
  }
}
$('volume').addEventListener('input', applyVolume);
$('fullscreen').addEventListener('click', () => {
  const card = $('screen-card');
  if (document.fullscreenElement) {
    document.exitFullscreen().catch(() => {});
  } else if (card.requestFullscreen) {
    card.requestFullscreen().catch((error) => log(`[página] pantalla completa no disponible: ${error.message}`));
  }
});
for (const [event, over] of [['dragenter', true], ['dragover', true], ['dragleave', false], ['drop', false]]) {
  $('drop').addEventListener(event, () => { $('drop').dataset.over = String(over); });
}

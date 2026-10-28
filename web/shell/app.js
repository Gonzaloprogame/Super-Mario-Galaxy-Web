// The page: checks what the engine needs from the browser, takes the user's game.pak and runs the engine in a worker.
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

function start() {
  const file = $('pak').files[0];
  if (!file) {
    setStatus('Selecciona primero tu game.pak.', 'error');
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
  setStatus('Listo. Selecciona tu game.pak para arrancar el motor.');
  document.body.dataset.state = 'ready';
}
$('run').addEventListener('click', start);

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

function start() {
  const file = $('pak').files[0];
  if (!file) {
    setStatus('Selecciona primero tu game.pak.', 'error');
    return;
  }
  const seconds = Math.max(1, Number($('seconds').value) || 10);
  $('run').disabled = true;
  logEl.textContent = '';
  setStatus(`Cargando ${file.name} (${(file.size / 2 ** 20).toFixed(1)} MB)…`);
  const worker = new Worker('smg_headless_web.js');
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
          setStatus('El motor terminó la ejecución de diagnóstico (código 0). Consulta el informe en el registro.', 'ok');
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
  worker.postMessage({ file, seconds });
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

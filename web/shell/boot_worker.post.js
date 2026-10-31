// Appended to smg_headless_web.js and smg_web.js (--extern-post-js): that script is the engine's worker. The page
// starts it with new Worker('smg_web.js') (or the diagnostic smg_headless_web.js); the pthreads' workers load the same
// script under the name "em-pthread" and run Emscripten's own pthread code, not this.
//
// Here the Emscripten program runs inside a dedicated worker: it may block (Atomics.wait) and it reads the user's
// game.pak in slices through WORKERFS, never copying the partition (about 4 GB) into memory.
// Messages to the page: {type: 'log' | 'status' | 'exit' | 'error', ...}.
if (globalThis.WorkerGlobalScope && globalThis.name !== 'em-pthread') {
  const post = (type, data) => self.postMessage(Object.assign({ type }, data));

  self.onmessage = async (event) => {
    const { file, seconds, language, label, nand, debug } = event.data;
    post('status', { text: 'Compilando e instanciando el módulo WebAssembly…' });
    let module;
    try {
      module = await createSmgModule({
        print: (text) => post('log', { text }),
        printErr: (text) => post('log', { text }),
        onExit: (code) => post('exit', { code }),
        onAbort: (what) => post('error', { text: `El motor abortó: ${what}` }),
      });
    } catch (error) {
      post('error', { text: `El módulo WebAssembly no se pudo instanciar: ${error}` });
      return;
    }
    module.FS.mkdir('/game');
    module.FS.mount(module.WORKERFS, { files: [file] }, '/game');
    post('status', { text: `Motor en marcha (${label}).` });
    try {
      // The NAND the page kept (IndexedDB), as a file for the host: web/host/headless_host.c, restore_nand
      let nandPath = '';
      if (nand) {
        module.FS.writeFile('/nand.bin', nand);
        nandPath = '/nand.bin';
      }
      module.callMain([`/game/${file.name}`, String(seconds), String(language), nandPath, debug ? 'debug' : '']);
    } catch (error) {
      // exit() unwinds with an ExitStatus; onExit has reported it.
      if (!(error && error.name === 'ExitStatus')) {
        post('error', { text: `Error en el motor: ${error}` });
      }
    }
  };
}

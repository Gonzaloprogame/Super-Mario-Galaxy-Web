// Paints on a <canvas> the frames the engine posts on the BroadcastChannel "smg-present": the XFB the game's video
// interface shows, as RGBA rows (web/host/aurora_bridge.cpp, present). A 2D canvas: it works without presenting
// WebGPU to a canvas, which Chromium without a GPU cannot do (docs/BACKEND_WEBGPU.md).
// globalThis.smgPresenter.frames counts the frames painted; presenter.onFatal(text), if set, hears the renderer stop
// on an error, presenter.onExit(code) the engine's host announce its end (web/host/headless_host.c, host_exit) and
// presenter.onNand(id, bytes) the engine's NAND after each change.
(() => {
  globalThis.smgAttachPresenter = (canvas) => {
    const context = canvas.getContext('2d');
    const presenter = { frames: 0, width: 0, height: 0 };
    const channel = new BroadcastChannel('smg-present');
    channel.onmessage = (event) => {
      if (event.data.type === 'fatal') {
        // The renderer stopped on an error (web/host/aurora_bridge.cpp)
        if (presenter.onFatal) {
          presenter.onFatal(event.data.text, event.data.stack);
        }
        return;
      }
      if (event.data.type === 'nand') {
        // The engine's NAND after a change (web/host/headless_host.c): the page keeps it
        if (presenter.onNand) {
          presenter.onNand(event.data.id, event.data.data);
        }
        return;
      }
      if (event.data.type === 'debug') {
        // The diagnostic view (?debug=1): the game's largest copy of the frame
        if (presenter.onDebug) {
          presenter.onDebug(event.data);
        }
        return;
      }
      if (event.data.type === 'exit') {
        if (presenter.onExit) {
          presenter.onExit(event.data.code);
        }
        return;
      }
      const { width, height, black, pixels } = event.data;
      if (canvas.width !== width || canvas.height !== height) {
        canvas.width = width;
        canvas.height = height;
      }
      if (black) {
        context.fillStyle = '#000';
        context.fillRect(0, 0, width, height);
      } else {
        context.putImageData(new ImageData(new Uint8ClampedArray(pixels), width, height), 0, 0);
      }
      presenter.frames++;
      presenter.width = width;
      presenter.height = height;
    };
    globalThis.smgPresenter = presenter;
    return presenter;
  };
})();

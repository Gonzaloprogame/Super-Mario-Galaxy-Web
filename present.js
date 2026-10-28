// Paints on a <canvas> the frames the engine posts on the BroadcastChannel "smg-present": the XFB the game's video
// interface shows, as RGBA rows (web/host/aurora_bridge.cpp, present). A 2D canvas: it works without presenting
// WebGPU to a canvas, which Chromium without a GPU cannot do (docs/BACKEND_WEBGPU.md).
// globalThis.smgPresenter.frames counts the frames painted.
(() => {
  globalThis.smgAttachPresenter = (canvas) => {
    const context = canvas.getContext('2d');
    const presenter = { frames: 0, width: 0, height: 0 };
    const channel = new BroadcastChannel('smg-present');
    channel.onmessage = (event) => {
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

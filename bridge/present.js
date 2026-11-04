// Paints on a <canvas> the frames the engine posts on the BroadcastChannel "smg-present": the XFB the game's video
// interface shows, as RGBA rows (web/host/aurora_bridge.cpp, present). A 2D canvas: it works without presenting
// WebGPU to a canvas, which Chromium without a GPU cannot do (docs/BACKEND_WEBGPU.md).
// globalThis.smgPresenter.frames counts the frames painted; presenter.onFatal(text), if set, hears the renderer stop
// on an error, presenter.onExit(code) the engine's host announce its end (web/host/headless_host.c, host_exit) and
// presenter.onNand(id, bytes) the engine's NAND after each change.
// presenter.attachMemory(buffer, address): the engine's frame slots in its shared memory (web/host/aurora_bridge.h,
// smg_bridge_present_memory). Marking them read makes the render thread write each frame there and post only which
// slot ({type: 'frame'}), instead of the rows in the message. A slot rewritten while it is read (its sequence changed)
// is dropped: presenter.dropped counts them, presenter.sharedFrames the frames painted from the slots.
(() => {
  globalThis.smgAttachPresenter = (canvas) => {
    const context = canvas.getContext('2d');
    const presenter = { frames: 0, width: 0, height: 0, sharedFrames: 0, dropped: 0 };
    // The slots' layout (web/host/aurora_bridge.cpp): a 16-byte header, then 3 slots of a 16-byte header and the rows
    const HEADER = 16;
    const SLOT_BYTES = HEADER + 640 * 576 * 4;
    let memory = null;
    let image = null; // reused while the size stays
    presenter.attachMemory = (buffer, address) => {
      memory = { buffer, address };
      presenter.memory = memory;
      Atomics.store(new Int32Array(buffer, address, 1), 0, 1);
    };
    const paint = (width, height, black, fill) => {
      if (canvas.width !== width || canvas.height !== height) {
        canvas.width = width;
        canvas.height = height;
      }
      if (black) {
        context.fillStyle = '#000';
        context.fillRect(0, 0, width, height);
      } else {
        context.putImageData(fill(), 0, 0);
      }
      presenter.frames++;
      presenter.width = width;
      presenter.height = height;
    };
    // A frame in a slot: copied out if its sequence is the one posted, before and after the copy
    const fromSlot = ({ width, height, black, slot, sequence }) => {
      if (!memory) {
        presenter.dropped++;
        return;
      }
      const base = memory.address + HEADER + slot * SLOT_BYTES;
      const word = new Int32Array(memory.buffer, base, 1);
      if (Atomics.load(word, 0) !== sequence) {
        presenter.dropped++;
        return;
      }
      if (!image || image.width !== width || image.height !== height) {
        image = new ImageData(width, height);
      }
      if (!black) {
        image.data.set(new Uint8Array(memory.buffer, base + HEADER, width * height * 4));
        if (Atomics.load(word, 0) !== sequence) {
          presenter.dropped++;
          return;
        }
      }
      paint(width, height, black, () => image);
      presenter.sharedFrames++;
    };
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
      if (event.data.type === 'framelog') {
        // A frame the renderer recorded on request (Aurora patch 0017): kept for whoever asked (globalThis.smgFrameLog)
        globalThis.smgFrameLog = event.data.text;
        return;
      }
      if (event.data.type === 'exit') {
        if (presenter.onExit) {
          presenter.onExit(event.data.code);
        }
        return;
      }
      if (event.data.type === 'frame') {
        fromSlot(event.data);
        return;
      }
      const { width, height, black, pixels } = event.data;
      paint(width, height, black, () => new ImageData(new Uint8ClampedArray(pixels), width, height));
    };
    globalThis.smgPresenter = presenter;
    return presenter;
  };
})();

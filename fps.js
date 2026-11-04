// The frame-rate badge over the screen: the frames per second the page really painted (presenter.frames, counted in
// web/shell/present.js for every frame put on the canvas), over the last second, and a small graph of the last
// ~24 seconds. Nothing is estimated: with no frames painted it shows 0.
// smgAttachFps(badge, presenter) fills the badge (an element in web/shell/index.html) and returns
// { fps, history, visible, setVisible(on) }; globalThis.smgFps is the same object, for the tests.
(() => {
  const SAMPLE_MS = 250; // how often the painted-frame count is read
  const WINDOW_MS = 1000; // the span the rate is measured over
  const HISTORY = 48; // points in the graph, one per half second
  // The tiers: the game runs at 60 on the console; 30 is still smooth, below it is slow
  const tierOf = (fps) => (fps >= 50 ? 'good' : fps >= 28 ? 'fair' : 'low');

  globalThis.smgAttachFps = (badge, presenter) => {
    badge.innerHTML =
      '<span class="fps-dot" aria-hidden="true"></span>' +
      '<span class="fps-read"><span class="fps-value">0</span><span class="fps-unit">FPS</span></span>' +
      '<canvas class="fps-graph" width="64" height="24" aria-hidden="true"></canvas>';
    const value = badge.querySelector('.fps-value');
    const graph = badge.querySelector('.fps-graph');
    const state = { fps: 0, history: [], visible: true };
    const samples = []; // [time, frames]
    let ticks = 0;
    let storedVisible = true;
    try { storedVisible = localStorage.getItem('smg-fps') !== 'off'; } catch { /* no storage */ }

    const draw = () => {
      const ratio = Math.min(globalThis.devicePixelRatio || 1, 3);
      const width = 64, height = 24;
      if (graph.width !== width * ratio) {
        graph.width = width * ratio;
        graph.height = height * ratio;
      }
      const g = graph.getContext('2d');
      g.setTransform(ratio, 0, 0, ratio, 0, 0);
      g.clearRect(0, 0, width, height);
      // The 30 and 60 marks
      g.strokeStyle = 'rgba(255,255,255,.12)';
      g.lineWidth = 1;
      for (const mark of [30, 60]) {
        const y = Math.round(height - 2 - (mark / 60) * (height - 4)) + 0.5;
        g.beginPath();
        g.moveTo(0, y);
        g.lineTo(width, y);
        g.stroke();
      }
      const points = state.history;
      if (points.length < 2) return;
      const color = getComputedStyle(badge).getPropertyValue('--fps-color').trim() || '#4cd6a0';
      const step = width / (HISTORY - 1);
      const x0 = width - (points.length - 1) * step;
      const y = (fps) => height - 2 - (Math.min(fps, 60) / 60) * (height - 4);
      g.beginPath();
      points.forEach((fps, i) => (i ? g.lineTo(x0 + i * step, y(fps)) : g.moveTo(x0, y(fps))));
      g.lineWidth = 1.6;
      g.lineJoin = 'round';
      g.strokeStyle = color;
      g.stroke();
      g.lineTo(width, height);
      g.lineTo(x0, height);
      g.closePath();
      const fill = g.createLinearGradient(0, 0, 0, height);
      fill.addColorStop(0, color + '66');
      fill.addColorStop(1, color + '00');
      g.fillStyle = fill;
      g.fill();
    };

    const sample = () => {
      const now = performance.now();
      samples.push([now, presenter.frames]);
      while (samples.length > 2 && now - samples[1][0] >= WINDOW_MS) samples.shift();
      const [then, frames] = samples[0];
      state.fps = now > then ? ((presenter.frames - frames) * 1000) / (now - then) : 0;
      // Hidden until the first frame: before it there is nothing to measure
      badge.hidden = !state.visible || presenter.frames === 0;
      if (++ticks % 2 !== 0) return;
      state.history.push(state.fps);
      if (state.history.length > HISTORY) state.history.shift();
      const shown = Math.round(state.fps);
      value.textContent = String(shown);
      badge.dataset.tier = tierOf(shown);
      badge.title = `${shown} fotogramas por segundo pintados en la página (último segundo); el juego va a 60 en la consola`;
      if (!badge.hidden) draw();
    };

    state.setVisible = (on) => {
      state.visible = on;
      badge.hidden = !on || presenter.frames === 0;
      try { localStorage.setItem('smg-fps', on ? 'on' : 'off'); } catch { /* no storage */ }
      if (!badge.hidden) draw();
    };
    state.visible = storedVisible;
    setInterval(sample, SAMPLE_MS);
    globalThis.smgFps = state;
    return state;
  };
})();

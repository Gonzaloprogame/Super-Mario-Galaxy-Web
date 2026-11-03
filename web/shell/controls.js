// The controls: one Wii Remote with a Nunchuk, from the screen's touch controls, the keyboard, the mouse and physical
// gamepads (the Gamepad API's standard mapping: Xbox, PlayStation and most Bluetooth pads in Chrome and Android). Their
// state goes straight into the engine's shared memory, in the words the host reads for each Wii Remote report
// (web/host/console_services.h, SMG_INPUT_*): the engine's thread builds the reports from them every 10 ms.
// Keyboard and mouse as Nebula's defaults (third_party/nebula/docs/KEYBOARD_CONTROLS.md).
// smgAttachControls(panel, canvas) builds the touch controls in `panel`; .attach(Int32Array) starts writing to the
// engine's words, .detach() stops. globalThis.smgControls is it (for the tests).
(() => {
  const BUTTON = {
    A: 0x0800, B: 0x0400, Z: 0x2000, C: 0x4000, PLUS: 0x0010, MINUS: 0x1000, HOME: 0x8000, ONE: 0x0200, TWO: 0x0100,
    UP: 0x0008, DOWN: 0x0004, LEFT: 0x0001, RIGHT: 0x0002,
  };
  const WORD = { BUTTONS: 0, STICK_X: 1, STICK_Y: 2, POINTER: 3, POINTER_X: 4, POINTER_Y: 5, SHAKE: 6 };
  const KEYS = {
    Space: { button: 'A' }, Enter: { button: 'A' }, NumpadEnter: { button: 'A' },
    ShiftLeft: { button: 'B' }, ShiftRight: { button: 'B' },
    KeyZ: { button: 'Z' }, ControlLeft: { button: 'Z' }, ControlRight: { button: 'Z' },
    KeyC: { button: 'C' },
    KeyQ: { shake: 1 }, KeyE: { shake: 1 },
    ArrowUp: { button: 'UP' }, ArrowDown: { button: 'DOWN' }, ArrowLeft: { button: 'LEFT' }, ArrowRight: { button: 'RIGHT' },
    Equal: { button: 'PLUS' }, KeyP: { button: 'PLUS' }, NumpadAdd: { button: 'PLUS' },
    Minus: { button: 'MINUS' }, KeyM: { button: 'MINUS' }, NumpadSubtract: { button: 'MINUS' },
    KeyH: { button: 'HOME' },
    Digit1: { button: 'ONE' }, Numpad1: { button: 'ONE' }, Digit2: { button: 'TWO' }, Numpad2: { button: 'TWO' },
    KeyW: { stick: [0, 1] }, KeyS: { stick: [0, -1] }, KeyA: { stick: [-1, 0] }, KeyD: { stick: [1, 0] },
  };
  // A gamepad in the standard mapping (w3c.github.io/gamepad, "Standard Gamepad"): its button index -> the Wii Remote's
  // and the Nunchuk's. The left stick is the Nunchuk's stick, the right stick moves the pointer.
  const PAD = {
    0: { button: 'A' }, 1: { button: 'B' }, 2: { shake: 1 }, 3: { shake: 1 },
    4: { button: 'C' }, 5: { shake: 1 }, 6: { button: 'Z' }, 7: { shake: 1 },
    8: { button: 'MINUS' }, 9: { button: 'PLUS' }, 10: { button: 'ONE' }, 11: { button: 'TWO' },
    12: { button: 'UP' }, 13: { button: 'DOWN' }, 14: { button: 'LEFT' }, 15: { button: 'RIGHT' }, 16: { button: 'HOME' },
  };
  const DEADZONE = 0.18;
  // A stick's axis beyond its dead zone, rescaled to start at 0 there
  const axis = (value) => (Math.abs(value) < DEADZONE ? 0 : Math.sign(value) * (Math.abs(value) - DEADZONE) / (1 - DEADZONE));

  globalThis.smgAttachControls = (panel, canvas) => {
    let view = null;
    const held = new Map(); // source (a key, a touch) -> {buttons, shake}
    const keyStick = new Set();
    let touchStick = null; // [x, y] while the touch stick is held
    let padStick = null; // [x, y] while a gamepad's left stick is out of its dead zone
    const pointer = { on: false, x: 0, y: 0 };

    const write = () => {
      if (!view) return;
      let buttons = 0;
      let shake = 0;
      for (const h of held.values()) {
        buttons |= h.buttons;
        shake |= h.shake;
      }
      let [x, y] = touchStick || padStick || [0, 0];
      if (!touchStick && !padStick) {
        for (const code of keyStick) {
          x += KEYS[code].stick[0];
          y += KEYS[code].stick[1];
        }
        const length = Math.hypot(x, y);
        if (length > 1) [x, y] = [x / length, y / length];
      }
      Atomics.store(view, WORD.BUTTONS, buttons);
      Atomics.store(view, WORD.STICK_X, Math.round(x * 100));
      Atomics.store(view, WORD.STICK_Y, Math.round(y * 100));
      Atomics.store(view, WORD.POINTER_X, Math.round(pointer.x * 10000));
      Atomics.store(view, WORD.POINTER_Y, Math.round(pointer.y * 10000));
      Atomics.store(view, WORD.POINTER, pointer.on ? 1 : 0);
      Atomics.store(view, WORD.SHAKE, shake);
    };
    const press = (source, buttons, shake = 0) => { held.set(source, { buttons, shake }); write(); };
    const release = (source) => { if (held.delete(source)) write(); };

    // Keyboard: only while the engine runs, and not while typing in the page's own fields
    const typing = (event) => /^(INPUT|SELECT|TEXTAREA)$/.test(event.target?.tagName || '');
    addEventListener('keydown', (event) => {
      const key = KEYS[event.code];
      if (!view || !key || typing(event)) return;
      event.preventDefault();
      if (key.stick) { keyStick.add(event.code); write(); return; }
      press(`key:${event.code}`, key.button ? BUTTON[key.button] : 0, key.shake || 0);
    });
    addEventListener('keyup', (event) => {
      const key = KEYS[event.code];
      if (!key) return;
      if (key.stick) { keyStick.delete(event.code); write(); return; }
      release(`key:${event.code}`);
    });
    addEventListener('blur', () => { held.clear(); keyStick.clear(); write(); });

    // The pointer: where the screen is touched (it stays there) or where the mouse is over it. The mouse's left
    // button is A, its right button B.
    const aim = (event) => {
      const box = canvas.getBoundingClientRect();
      pointer.x = Math.min(1, Math.max(-1, ((event.clientX - box.left) / box.width) * 2 - 1));
      pointer.y = Math.min(1, Math.max(-1, ((event.clientY - box.top) / box.height) * 2 - 1));
      pointer.on = true;
      write();
    };
    canvas.addEventListener('pointerdown', (event) => {
      event.preventDefault();
      aim(event);
      if (event.pointerType === 'mouse') {
        press(`mouse:${event.button}`, event.button === 2 ? BUTTON.B : event.button === 0 ? BUTTON.A : 0);
      } else {
        canvas.setPointerCapture(event.pointerId);
      }
    });
    canvas.addEventListener('pointermove', (event) => {
      if (event.pointerType === 'mouse' || canvas.hasPointerCapture(event.pointerId)) aim(event);
    });
    canvas.addEventListener('pointerup', (event) => { if (event.pointerType === 'mouse') release(`mouse:${event.button}`); });
    canvas.addEventListener('pointerleave', (event) => {
      if (event.pointerType !== 'mouse') return;
      pointer.on = false; // pointing away from the screen
      release('mouse:0');
      release('mouse:2');
      write();
    });
    canvas.addEventListener('contextmenu', (event) => event.preventDefault());

    // The touch controls: the Nunchuk's stick on the left; A, B, Z, C, the shake and the D-pad on the right; Minus,
    // HOME, Plus, 1 and 2 below
    panel.innerHTML = `
      <div class="pad-stick" aria-label="Palanca del Nunchuk"><div class="pad-knob"></div></div>
      <div class="pad-dpad">
        <button type="button" data-button="UP" aria-label="Arriba">▲</button>
        <button type="button" data-button="LEFT" aria-label="Izquierda">◀</button>
        <button type="button" data-button="RIGHT" aria-label="Derecha">▶</button>
        <button type="button" data-button="DOWN" aria-label="Abajo">▼</button>
      </div>
      <div class="pad-main">
        <button type="button" data-button="Z" class="pad-small">Z</button>
        <button type="button" data-button="C" class="pad-small">C</button>
        <button type="button" data-shake="1" class="pad-shake">Agitar</button>
        <button type="button" data-button="B" class="pad-b">B</button>
        <button type="button" data-button="A" class="pad-a">A</button>
      </div>
      <div class="pad-system">
        <button type="button" data-button="MINUS" aria-label="Menos">−</button>
        <button type="button" data-button="HOME" aria-label="HOME">⌂</button>
        <button type="button" data-button="PLUS" aria-label="Más">+</button>
        <button type="button" data-button="ONE">1</button>
        <button type="button" data-button="TWO">2</button>
      </div>`;
    for (const button of panel.querySelectorAll('button')) {
      const source = (event) => `touch:${event.pointerId}`;
      button.addEventListener('pointerdown', (event) => {
        event.preventDefault();
        button.setPointerCapture(event.pointerId);
        button.dataset.held = 'true';
        press(source(event), button.dataset.button ? BUTTON[button.dataset.button] : 0, Number(button.dataset.shake || 0));
      });
      const up = (event) => { delete button.dataset.held; release(source(event)); };
      button.addEventListener('pointerup', up);
      button.addEventListener('pointercancel', up);
      button.addEventListener('contextmenu', (event) => event.preventDefault());
    }
    const stick = panel.querySelector('.pad-stick');
    const knob = panel.querySelector('.pad-knob');
    let stickPointer = null;
    const moveStick = (event) => {
      const box = stick.getBoundingClientRect();
      const radius = box.width / 2;
      let x = (event.clientX - box.left - radius) / radius;
      let y = -(event.clientY - box.top - radius) / radius;
      const length = Math.hypot(x, y);
      if (length > 1) [x, y] = [x / length, y / length];
      touchStick = [x, y];
      knob.style.transform = `translate(${x * radius * 0.6}px, ${-y * radius * 0.6}px)`;
      write();
    };
    stick.addEventListener('pointerdown', (event) => {
      event.preventDefault();
      stickPointer = event.pointerId;
      stick.setPointerCapture(event.pointerId);
      moveStick(event);
    });
    stick.addEventListener('pointermove', (event) => { if (event.pointerId === stickPointer) moveStick(event); });
    const stickUp = (event) => {
      if (event.pointerId !== stickPointer) return;
      stickPointer = null;
      touchStick = null;
      knob.style.transform = '';
      write();
    };
    stick.addEventListener('pointerup', stickUp);
    stick.addEventListener('pointercancel', stickUp);

    // Gamepads: read at each animation frame while the engine runs (the Gamepad API has no events for buttons). Each
    // pad's buttons are their own sources, so they combine with the keyboard and the screen's buttons.
    const padHeld = new Set();
    let padFrame = 0, padTime = 0;
    const pads = { connected: 0, last: '' }; // for the tests and the page's help
    const pollPads = (time) => {
      padFrame = 0;
      if (!view) return;
      const dt = padTime ? Math.min(0.1, (time - padTime) / 1000) : 0;
      padTime = time;
      const list = (navigator.getGamepads ? Array.from(navigator.getGamepads()) : []).filter((p) => p && p.connected);
      pads.connected = list.length;
      let stickX = 0, stickY = 0, aimX = 0, aimY = 0;
      const now = new Set();
      for (const pad of list) {
        pads.last = pad.id;
        if (pad.mapping !== 'standard') continue; // unknown layouts: their buttons mean nothing certain
        pad.buttons.forEach((b, index) => {
          if (PAD[index] && (b.pressed || b.value > 0.5)) now.add(`pad:${pad.index}:${index}`);
        });
        stickX += axis(pad.axes[0] || 0);
        stickY -= axis(pad.axes[1] || 0); // the pad's up is negative
        aimX += axis(pad.axes[2] || 0);
        aimY += axis(pad.axes[3] || 0);
      }
      let changed = false;
      for (const source of now) {
        if (!padHeld.has(source)) {
          const key = PAD[Number(source.split(':')[2])];
          held.set(source, { buttons: key.button ? BUTTON[key.button] : 0, shake: key.shake || 0 });
          padHeld.add(source);
          changed = true;
        }
      }
      for (const source of [...padHeld]) {
        if (!now.has(source)) {
          held.delete(source);
          padHeld.delete(source);
          changed = true;
        }
      }
      const length = Math.hypot(stickX, stickY);
      const next = length > 0 ? (length > 1 ? [stickX / length, stickY / length] : [stickX, stickY]) : null;
      if (String(next) !== String(padStick)) {
        padStick = next;
        changed = true;
      }
      if (aimX !== 0 || aimY !== 0) {
        // The right stick moves the pointer across the screen in about a second at full tilt
        pointer.x = Math.min(1, Math.max(-1, pointer.x + aimX * dt * 2));
        pointer.y = Math.min(1, Math.max(-1, pointer.y + aimY * dt * 2));
        pointer.on = true;
        changed = true;
      }
      if (changed) write();
      padFrame = requestAnimationFrame(pollPads);
    };
    const startPads = () => { if (!padFrame && view) padFrame = requestAnimationFrame(pollPads); };
    addEventListener('gamepadconnected', startPads);

    const controls = {
      attach(words) { view = words; controls.words = words; write(); startPads(); },
      words: null, // the engine's words last attached (its last word: the host's mark, "SMGI")
      detach() {
        view = null;
        for (const source of padHeld) held.delete(source);
        padHeld.clear();
        padStick = null;
        cancelAnimationFrame(padFrame);
        padFrame = 0;
        padTime = 0;
      },
      pads,
      get attached() { return view !== null; },
      pointer,
    };
    globalThis.smgControls = controls;
    return controls;
  };
})();

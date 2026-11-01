// The game's sound: the ring of 16-bit stereo samples the engine's audio interface fills (web/host/audio_output.h),
// in the engine's shared memory, played at the browser's rate. The game's rate (32 or 48 kHz, the ring's `rate`) is
// resampled linearly. Before playing, and after running dry, it waits for a little sound to build up; far behind, it
// skips ahead, so the sound stays close to the picture.
const HEADER_WORDS = 8; // magic, capacity, rate, write, read, dropped, reserved x2
const START_FRAMES = 1536;
const MAX_FRAMES = 6144;

class SmgAudio extends AudioWorkletProcessor {
  constructor() {
    super();
    this.header = null;
    this.samples = null;
    this.capacity = 0;
    this.playing = false;
    this.frac = 0;
    this.volume = 1;
    this.port.onmessage = ({ data }) => {
      if (data.type === 'ring') {
        this.header = new Int32Array(data.buffer, data.address, HEADER_WORDS);
        this.capacity = this.header[1];
        this.samples = new Int16Array(data.buffer, data.address + HEADER_WORDS * 4, this.capacity * 2);
      } else if (data.type === 'volume') {
        this.volume = data.value;
      }
    };
  }

  process(inputs, outputs) {
    const out = outputs[0];
    const left = out[0];
    const right = out[1] || out[0];
    if (this.header === null) {
      return true;
    }
    const write = Atomics.load(this.header, 3);
    let read = Atomics.load(this.header, 4);
    let pending = (write - read) | 0;
    if (pending > MAX_FRAMES) {
      read = (write - START_FRAMES) | 0;
      pending = START_FRAMES;
    }
    if (!this.playing && pending < START_FRAMES) {
      return true;
    }
    this.playing = true;
    const rate = this.header[2] || 32000;
    const step = rate / sampleRate;
    const scale = this.volume / 32768;
    for (let i = 0; i < left.length; i++) {
      if (pending < 2) {
        this.playing = false; // dry: silence until enough builds up again
        left[i] = 0;
        right[i] = 0;
        continue;
      }
      const a = ((read >>> 0) % this.capacity) * 2;
      const b = (((read + 1) >>> 0) % this.capacity) * 2;
      const t = this.frac;
      left[i] = (this.samples[a] + (this.samples[b] - this.samples[a]) * t) * scale;
      right[i] = (this.samples[a + 1] + (this.samples[b + 1] - this.samples[a + 1]) * t) * scale;
      this.frac += step;
      while (this.frac >= 1) {
        this.frac -= 1;
        read = (read + 1) | 0;
        pending--;
      }
    }
    Atomics.store(this.header, 4, read);
    return true;
  }
}

registerProcessor('smg-audio', SmgAudio);

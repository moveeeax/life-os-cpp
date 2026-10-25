/*
 * Ani's voice: plays a recorded line through WebAudio and reads it as it
 * plays, for the lips. Loudness opens the jaw; where the sound's energy sits
 * (its spectral centroid) picks the vowel shape: low for o and u, high for
 * i and e, a in between. Works with any recording, no viseme data needed.
 */

export interface Mouth {
  /** 0..1, how open. */
  open: number;
  /** Vowel weights for the PMX morphs あ い う え お. */
  a: number;
  i: number;
  u: number;
  e: number;
  o: number;
}

export interface Voice {
  /** Plays a line; resolves when it ends (or at once if it cannot play). */
  say(url: string): Promise<void>;
  /** Follows a live stream (played elsewhere) for the lips; null ends it. */
  follow(stream: MediaStream | null): void;
  /** The mouth for this frame. */
  read(dt: number): Mouth;
  readonly speaking: boolean;
  stop(): void;
  dispose(): void;
}

const SHUT: Mouth = { open: 0, a: 0, i: 0, u: 0, e: 0, o: 0 };

export function createVoice(): Voice {
  let ctx: AudioContext | null = null;
  let analyser: AnalyserNode | null = null;
  let src: AudioBufferSourceNode | null = null;
  let live: MediaStreamAudioSourceNode | null = null;
  // Analyses without playing: the live stream plays through its own element.
  let tap: AnalyserNode | null = null;
  let speaking = false;
  const buffers = new Map<string, Promise<AudioBuffer>>();
  let wave = new Float32Array(0);
  let spec = new Float32Array(0);
  const mouth: Mouth = { ...SHUT };

  const ensure = () => {
    if (!ctx) {
      ctx = new AudioContext();
      analyser = ctx.createAnalyser();
      analyser.fftSize = 1024;
      analyser.smoothingTimeConstant = 0.5;
      analyser.connect(ctx.destination);
      wave = new Float32Array(analyser.fftSize);
      spec = new Float32Array(analyser.frequencyBinCount);
    }
    return ctx;
  };

  const load = (url: string) => {
    let b = buffers.get(url);
    if (!b) {
      b = fetch(url)
        .then((r) => {
          if (!r.ok) throw new Error(`${r.status} ${url}`);
          return r.arrayBuffer();
        })
        .then((data) => ensure().decodeAudioData(data));
      buffers.set(url, b);
    }
    return b;
  };

  const stop = () => {
    if (src) {
      src.onended = null;
      try {
        src.stop();
      } catch {
        // Already stopped.
      }
      src.disconnect();
    }
    src = null;
    speaking = false;
  };

  return {
    get speaking() {
      return speaking;
    },
    follow(stream) {
      live?.disconnect();
      live = null;
      if (!stream) return;
      const c = ensure();
      void c.resume();
      if (!tap) {
        tap = c.createAnalyser();
        tap.fftSize = analyser!.fftSize;
        tap.smoothingTimeConstant = 0.5;
      }
      live = c.createMediaStreamSource(stream);
      live.connect(tap);
    },
    async say(url) {
      try {
        const c = ensure();
        // Browsers start audio only after a gesture; a tap resumes it.
        if (c.state === 'suspended') await c.resume();
        const buf = await load(url);
        stop();
        const s = c.createBufferSource();
        s.buffer = buf;
        s.connect(analyser!);
        src = s;
        speaking = true;
        await new Promise<void>((done) => {
          s.onended = () => {
            if (src === s) {
              src = null;
              speaking = false;
            }
            done();
          };
          s.start();
        });
      } catch {
        speaking = false;
      }
    },
    read(dt) {
      let open = 0;
      let centroid = 0.5;
      const an = live ? tap : speaking ? analyser : null;
      if (an && ctx) {
        an.getFloatTimeDomainData(wave);
        let sum = 0;
        for (const x of wave) sum += x * x;
        const rms = Math.sqrt(sum / wave.length);
        // Speech sits around 0.02-0.2 RMS; below the floor is a pause.
        open = Math.min(1, Math.max(0, (rms - 0.015) * 7));
        an.getFloatFrequencyData(spec);
        // Centroid over 200 Hz-4 kHz, where the vowels differ.
        const hz = ctx.sampleRate / an.fftSize;
        let num = 0;
        let den = 0;
        for (let k = Math.floor(200 / hz); k < Math.min(spec.length, 4000 / hz); k++) {
          const p = Math.pow(10, spec[k] / 20);
          num += p * k * hz;
          den += p;
        }
        if (den > 0) centroid = Math.min(1, Math.max(0, (num / den - 600) / 1800));
      }
      // Lips move fast but not in steps: opening quicker than closing.
      const rate = open > mouth.open ? 28 : 16;
      mouth.open += (open - mouth.open) * Math.min(1, dt * rate);
      const w = mouth.open;
      const lo = Math.max(0, 1 - centroid * 2.2);
      const hi = Math.max(0, centroid * 2.2 - 1.2);
      const mid = Math.max(0, 1 - lo - hi);
      const target: Mouth = {
        open: w,
        a: w * mid,
        o: w * lo * 0.7,
        u: w * lo * 0.3,
        e: w * hi * 0.6,
        i: w * hi * 0.4,
      };
      for (const k of ['a', 'i', 'u', 'e', 'o'] as const)
        mouth[k] += (target[k] - mouth[k]) * Math.min(1, dt * 20);
      return mouth;
    },
    stop,
    dispose() {
      stop();
      live?.disconnect();
      void ctx?.close();
      ctx = null;
    },
  };
}

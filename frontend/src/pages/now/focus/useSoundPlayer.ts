import { useCallback, useEffect, useRef, useState } from 'react';

import { FADE_MS, fadeVolume, type Sound } from '@/lib/now';

const SOUND_KEY = 'now:focus:sound';
const VOLUME_KEY = 'now:focus:volume';

function stored(key: string): string | null {
  try {
    return globalThis.localStorage?.getItem(key) ?? null;
  } catch {
    return null;
  }
}

function store(key: string, value: string) {
  try {
    globalThis.localStorage?.setItem(key, value);
  } catch {
    // Remembering the choice is a convenience.
  }
}

export type PlayerState = 'off' | 'playing' | 'blocked' | 'failed';

/**
 * One looping sound with fades. play() must run inside a click: browsers start
 * sound only on a user gesture; a refused start reads 'blocked', a file that
 * does not load reads 'failed', and the session goes on in silence.
 */
export function useSoundPlayer(sounds: Sound[]) {
  const audio = useRef<HTMLAudioElement | null>(null);
  const fade = useRef<number | null>(null);
  const [soundId, setSoundId] = useState<string | null>(() => stored(SOUND_KEY));
  const [volume, setVolumeState] = useState<number>(() => {
    const v = Number(stored(VOLUME_KEY));
    return Number.isFinite(v) && v > 0 && v <= 1 ? v : 0.6;
  });
  const [state, setState] = useState<PlayerState>('off');
  const sound = sounds.find((s) => s.id === soundId) ?? sounds[0] ?? null;

  // The end of a fade-out under way: a cut fade still stops its element, or
  // the old loop would play on with nothing pointing at it.
  const fadingOut = useRef<(() => void) | null>(null);

  const stopFade = () => {
    if (fade.current !== null) cancelAnimationFrame(fade.current);
    fade.current = null;
    const end = fadingOut.current;
    fadingOut.current = null;
    end?.();
  };

  const ramp = useCallback(
    (el: HTMLAudioElement, target: number, direction: 'in' | 'out', done?: () => void) => {
      stopFade();
      const t0 = performance.now();
      const step = () => {
        const elapsed = performance.now() - t0;
        el.volume = fadeVolume(target, elapsed, FADE_MS, direction);
        if (elapsed < FADE_MS) fade.current = requestAnimationFrame(step);
        else {
          fade.current = null;
          fadingOut.current = null;
          done?.();
        }
      };
      if (direction === 'out') fadingOut.current = done ?? null;
      fade.current = requestAnimationFrame(step);
    },
    [],
  );

  const play = useCallback(
    (which: Sound | null = sound) => {
      if (!which) return;
      stopFade();
      audio.current?.pause();
      const el = new Audio(which.url);
      el.loop = true;
      el.volume = 0;
      // Answers of an element that was stopped or replaced meanwhile change nothing.
      const current = () => audio.current === el;
      el.onerror = () => {
        if (current()) setState('failed');
      };
      audio.current = el;
      el.play().then(
        () => {
          if (!current()) {
            el.pause();
            return;
          }
          setState('playing');
          ramp(el, volume, 'in');
        },
        (e: unknown) => {
          if (!current()) return;
          const name = e instanceof DOMException ? e.name : '';
          if (name === 'AbortError') return;
          setState(name === 'NotAllowedError' ? 'blocked' : 'failed');
        },
      );
    },
    [sound, volume, ramp],
  );

  /** Stop: the player is off at once, the sound fades out (or stops at once with @p now). */
  const stop = useCallback(
    (now = false) => {
      const el = audio.current;
      audio.current = null;
      setState('off');
      if (!el) return;
      const end = () => {
        el.pause();
        el.onerror = null;
      };
      if (now || el.paused) {
        stopFade();
        end();
      } else ramp(el, el.volume, 'out', end);
    },
    [ramp],
  );

  const choose = useCallback(
    (id: string) => {
      setSoundId(id);
      store(SOUND_KEY, id);
      const next = sounds.find((s) => s.id === id) ?? null;
      if (state === 'playing' || state === 'blocked' || state === 'failed') play(next);
    },
    [sounds, state, play],
  );

  const setVolume = useCallback((v: number) => {
    setVolumeState(v);
    store(VOLUME_KEY, String(v));
    stopFade();
    if (audio.current) audio.current.volume = v;
  }, []);

  useEffect(
    () => () => {
      stopFade();
      audio.current?.pause();
    },
    [],
  );

  return { sound, sounds, state, volume, play, stop, choose, setVolume };
}

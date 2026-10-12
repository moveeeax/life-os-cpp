import { useEffect, useRef, useState } from 'react';

import { useFocusDone } from '@/hooks/useNow';
import { clockOf, type Focus, type Phase } from '@/lib/now';

import type { useSoundPlayer } from './useSoundPlayer';
import './focus.css';

const IDLE_MS = 3000;

function isPhone(): boolean {
  return typeof matchMedia === 'function' && matchMedia('(max-width: 640px)').matches;
}

interface Props {
  phase: Phase;
  left: number;
  focus: Focus | null;
  player: ReturnType<typeof useSoundPlayer>;
  onPause: () => void;
  onResume: () => void;
  onExit: () => void;
  onAgain: () => void;
}

/**
 * Focus mode: everything but the wallpaper recedes; a big countdown and the
 * day's focus stay, a sound plays. The thin bar shows on movement and fades
 * after three quiet seconds (it stays on a phone).
 */
export function FocusMode({
  phase,
  left,
  focus,
  player,
  onPause,
  onResume,
  onExit,
  onAgain,
}: Props) {
  const [awake, setAwake] = useState(true);
  const timer = useRef<number | null>(null);
  const done = useFocusDone();
  const [ticked, setTicked] = useState(false);

  useEffect(() => {
    document.documentElement.classList.add('focus-mode');
    return () => document.documentElement.classList.remove('focus-mode');
  }, []);

  useEffect(() => {
    const wake = () => {
      setAwake(true);
      if (timer.current !== null) window.clearTimeout(timer.current);
      if (!isPhone()) timer.current = window.setTimeout(() => setAwake(false), IDLE_MS);
    };
    const onKey = (e: KeyboardEvent) => {
      if (e.key === 'Escape') onExit();
      else wake();
    };
    wake();
    window.addEventListener('pointermove', wake);
    window.addEventListener('pointerdown', wake);
    window.addEventListener('keydown', onKey);
    return () => {
      if (timer.current !== null) window.clearTimeout(timer.current);
      window.removeEventListener('pointermove', wake);
      window.removeEventListener('pointerdown', wake);
      window.removeEventListener('keydown', onKey);
    };
  }, [onExit]);

  // The tab title carries the time left.
  useEffect(() => {
    document.title = phase === 'done' ? 'Done · Now' : `${clockOf(left)} · Focus`;
    return () => {
      document.title = 'Now';
    };
  }, [phase, left]);

  // The sound fades out when the time is up.
  const stop = player.stop;
  useEffect(() => {
    if (phase === 'done') stop();
  }, [phase, stop]);

  const tick = () => {
    if (!focus || focus.done || ticked) return;
    setTicked(true);
    done.mutate({ date: focus.date, done: true }, { onError: () => setTicked(false) });
  };

  return (
    <div
      className={awake || phase !== 'running' ? 'f-mode f-awake' : 'f-mode'}
      role="region"
      aria-label="Focus mode"
    >
      <div className="f-center">
        {phase === 'done' ? (
          <>
            <div className="f-done">Done.</div>
            {focus && !focus.done && !ticked ? (
              <button type="button" className="f-tick" onClick={tick}>
                <span className="f-ring" aria-hidden="true" /> Tick “{focus.text}”
              </button>
            ) : (
              focus && <div className="f-text f-struck">{focus.text}</div>
            )}
          </>
        ) : (
          <>
            <div className="f-clock" aria-live="off">
              {clockOf(left)}
            </div>
            {focus && <div className="f-text">{focus.text}</div>}
            {phase === 'paused' && <div className="f-paused">Paused</div>}
          </>
        )}
      </div>

      <div className="f-bar" aria-label="Focus controls">
        {phase === 'done' ? (
          <button type="button" onClick={onAgain}>
            Another round
          </button>
        ) : phase === 'paused' ? (
          <button type="button" onClick={onResume}>
            Resume
          </button>
        ) : (
          <button type="button" onClick={onPause}>
            Pause
          </button>
        )}
        {player.sounds.length > 0 && (
          <>
            <label className="f-sr" htmlFor="focus-sound">
              Sound
            </label>
            <select
              id="focus-sound"
              value={player.sound?.id ?? ''}
              onChange={(e) => player.choose(e.target.value)}
            >
              {player.sounds.map((s) => (
                <option key={s.id} value={s.id}>
                  {s.title}
                </option>
              ))}
            </select>
            {player.state === 'playing' ? (
              <button type="button" onClick={() => player.stop()}>
                Mute
              </button>
            ) : (
              phase !== 'done' && (
                <button type="button" onClick={() => player.play()}>
                  Play sound
                </button>
              )
            )}
            <label className="f-sr" htmlFor="focus-volume">
              Volume
            </label>
            <input
              id="focus-volume"
              type="range"
              min={0.05}
              max={1}
              step={0.05}
              value={player.volume}
              onChange={(e) => player.setVolume(Number(e.target.value))}
            />
          </>
        )}
        <button type="button" onClick={onExit}>
          Exit
        </button>
        {player.state === 'failed' && <span className="f-note">This sound could not play.</span>}
        {player.sounds.length === 0 && (
          <span className="f-note">No sounds yet: add them in Admin → Library.</span>
        )}
      </div>
    </div>
  );
}

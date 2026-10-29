import { useState } from 'react';

import { FOCUS_LENGTHS } from '@/lib/now';

const LENGTH_KEY = 'now:focus:length';

/** "Focus" and its length under the day's focus; the chosen length is filled. */
export function FocusStart({ onStart }: { onStart: (minutes: number) => void }) {
  const [minutes, setMinutes] = useState<number>(() => {
    try {
      const v = Number(globalThis.localStorage?.getItem(LENGTH_KEY));
      return (FOCUS_LENGTHS as readonly number[]).includes(v) ? v : 25;
    } catch {
      return 25;
    }
  });
  const pick = (m: number) => {
    setMinutes(m);
    try {
      globalThis.localStorage?.setItem(LENGTH_KEY, String(m));
    } catch {
      // Remembering the length is a convenience.
    }
  };
  return (
    <div className="n-under">
      <button type="button" className="n-start" onClick={() => onStart(minutes)}>
        Focus
      </button>
      <span className="n-lengths" role="group" aria-label="Focus length" data-num-ok>
        {FOCUS_LENGTHS.map((m) => (
          <button key={m} type="button" aria-pressed={m === minutes} onClick={() => pick(m)}>
            {m} min
          </button>
        ))}
      </span>
    </div>
  );
}

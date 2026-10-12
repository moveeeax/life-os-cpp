import { useCallback, useEffect, useState } from 'react';

import { FOCUS_MINUTES, readCache, timerKey, timerState, writeCache } from '@/lib/now';

/** The 25-minute focus timer, its end time kept per user so a new tab shows the same time left. */
export function useFocusTimer(userId: string | null) {
  const key = userId ? timerKey(userId) : null;
  const [endAt, setEndAt] = useState<number | null>(() => (key ? readCache<number>(key) : null));
  const [now, setNow] = useState(() => Date.now());
  const state = timerState(endAt, now);

  useEffect(() => {
    if (!state.running) return;
    const id = setInterval(() => setNow(Date.now()), 1000);
    return () => clearInterval(id);
  }, [state.running]);

  useEffect(() => {
    const base = 'Now';
    if (state.running) {
      const mm = String(Math.floor(state.left / 60)).padStart(2, '0');
      const ss = String(state.left % 60).padStart(2, '0');
      document.title = `${mm}:${ss} · ${base}`;
    } else if (state.done && now - (endAt ?? 0) < 10_000) {
      document.title = `Done · ${base}`;
    } else {
      document.title = base;
    }
  }, [state.running, state.done, state.left, now, endAt]);

  const set = useCallback(
    (v: number | null) => {
      setEndAt(v);
      setNow(Date.now());
      if (key) writeCache(key, v);
    },
    [key],
  );

  return {
    ...state,
    start: () => set(Date.now() + FOCUS_MINUTES * 60_000),
    stop: () => set(null),
  };
}

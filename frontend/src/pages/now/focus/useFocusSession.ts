import { useCallback, useEffect, useState } from 'react';

import {
  focusKey,
  pauseSession,
  readCache,
  resumeSession,
  sessionState,
  startSession,
  writeCache,
  type Session,
} from '@/lib/now';

/** The focus session of this user, kept in the browser so a reload carries on. */
export function useFocusSession(userId: string | null) {
  const key = userId ? focusKey(userId) : null;
  const [session, setSession] = useState<Session | null>(() =>
    key ? readCache<Session>(key) : null,
  );
  const [now, setNow] = useState(() => Date.now());
  const state = sessionState(session, now);

  useEffect(() => {
    if (state.phase !== 'running') return;
    const id = setInterval(() => setNow(Date.now()), 1000);
    return () => clearInterval(id);
  }, [state.phase]);

  const save = useCallback(
    (s: Session | null) => {
      setSession(s);
      setNow(Date.now());
      if (key) writeCache(key, s);
    },
    [key],
  );

  return {
    ...state,
    start: (minutes: number) => save(startSession(minutes, Date.now())),
    pause: () => session && save(pauseSession(session, Date.now())),
    resume: () => session && save(resumeSession(session, Date.now())),
    exit: () => save(null),
  };
}

import { useCallback, useEffect, useState } from 'react';

import { api } from '@/lib/api/client';
import {
  focusKey,
  focusRecord,
  pauseSession,
  readCache,
  resumeSession,
  sessionFromStorage,
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

  // A start, pause or exit in another tab shows up here too.
  useEffect(() => {
    if (!key) return;
    const onStorage = (e: StorageEvent) => {
      const s = sessionFromStorage(e, key);
      if (s === undefined) return;
      setSession(s);
      setNow(Date.now());
    };
    window.addEventListener('storage', onStorage);
    return () => window.removeEventListener('storage', onStorage);
  }, [key]);

  // A finished session is kept on the server once, for the day ribbon.
  const record = useCallback((s: Session | null, at: number) => {
    const r = focusRecord(s, at);
    if (!r) return false;
    void api.postJson('/api/v1/now/focus-sessions', { body: r }).catch(() => {});
    return true;
  }, []);

  const save = useCallback(
    (s: Session | null) => {
      setSession(s);
      setNow(Date.now());
      if (key) writeCache(key, s);
    },
    [key],
  );

  useEffect(() => {
    if (state.phase === 'done' && session && record(session, Date.now())) {
      const kept = { ...session, recorded: true };
      setSession(kept);
      if (key) writeCache(key, kept);
    }
  }, [state.phase, session, record, key]);

  return {
    ...state,
    /** The day of the focus this session is for, so Done only ticks that one. */
    focusDate: session?.focusDate ?? null,
    start: (minutes: number, focusDate: string | null = null) =>
      save(startSession(minutes, Date.now(), focusDate)),
    pause: () => session && save(pauseSession(session, Date.now())),
    resume: () => session && save(resumeSession(session, Date.now())),
    exit: () => {
      // Left early: what was done still counts (five minutes or more).
      if (state.phase === 'running' || state.phase === 'paused') record(session, Date.now());
      save(null);
    },
  };
}

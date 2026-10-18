import { useCallback, useEffect, useRef, useState, useSyncExternalStore } from 'react';
import {
  useInfiniteQuery,
  useQuery,
  useQueryClient,
  type QueryClient,
} from '@tanstack/react-query';

import { useApiMutation } from '@/hooks/useApiMutation';
import { browserStore as browserStorage } from '@/lib/browserStore';
import { ApiClientError, api, apiErrorMessage } from '@/lib/api/client';
import { qk } from '@/lib/api/queryKeys';
import {
  backoffMs,
  dropFromQueue,
  enqueue,
  isPermanentFailure,
  markFailed,
  readQueue,
  readRestEnd,
  restRemaining,
  writeQueue,
  writeRestEnd,
  type QueuedSet,
  type SetStore,
} from '@/lib/workout/session';
import type {
  Exercise,
  WorkoutHeartRate,
  WorkoutReadiness,
  WorkoutSession,
  WorkoutSessionListResponse,
  WorkoutSessionPatch,
  WorkoutSet,
  WorkoutSetInput,
} from '@/lib/workout/types';

const BASE = '/api/v1/workout';

/** A store that keeps nothing: the queue then lives in memory for this page. */
const NO_STORE: SetStore = { getItem: () => null, setItem: () => {}, removeItem: () => {} };

/** The browser's storage, or NO_STORE when it is blocked or absent. */
const browserStore = (): SetStore => browserStorage() ?? NO_STORE;

/**
 * Puts a session the server just returned into the cache: under its own key
 * and, while it is unfinished, as the active one.
 */
function cacheSession(qc: QueryClient, session: WorkoutSession) {
  qc.setQueryData(qk.workout.session(session.id), session);
  const active = qc.getQueryData<WorkoutSession | null>(qk.workout.active());
  if (session.finished_at === null) qc.setQueryData(qk.workout.active(), session);
  else if (active?.id === session.id) qc.setQueryData(qk.workout.active(), null);
}

/** Applies `change` to the cached copies of a session. */
function patchCachedSession(
  qc: QueryClient,
  sessionId: string,
  change: (session: WorkoutSession) => WorkoutSession,
) {
  qc.setQueryData<WorkoutSession>(qk.workout.session(sessionId), (s) => (s ? change(s) : s));
  qc.setQueryData<WorkoutSession | null>(qk.workout.active(), (s) =>
    s && s.id === sessionId ? change(s) : s,
  );
}

const withSet = (session: WorkoutSession, set: WorkoutSet): WorkoutSession => ({
  ...session,
  exercises: session.exercises.map((e) =>
    e.id === set.session_exercise_id
      ? {
          ...e,
          sets: [...e.sets.filter((s) => s.id !== set.id), set].sort(
            (a, b) => a.position - b.position,
          ),
        }
      : e,
  ),
});

const withoutSet = (session: WorkoutSession, setId: string): WorkoutSession => ({
  ...session,
  exercises: session.exercises.map((e) => ({ ...e, sets: e.sets.filter((s) => s.id !== setId) })),
});

// ── queries ─────────────────────────────────────────────────────────────────

/** The unfinished session, or null. */
export function useActiveSession() {
  return useQuery({
    queryKey: qk.workout.active(),
    queryFn: async ({ signal }) =>
      (await api.getJson<{ data: WorkoutSession | null }>(`${BASE}/sessions/active`, { signal }))
        .data,
  });
}

export function useSession(id: string) {
  return useQuery({
    queryKey: qk.workout.session(id),
    queryFn: async ({ signal }) =>
      (await api.getJson<{ data: WorkoutSession }>(`${BASE}/sessions/${id}`, { signal })).data,
    // While the band data is awaited the session is asked again now and then.
    refetchInterval: (query) => (query.state.data?.health_status === 'pending' ? 60000 : false),
  });
}

export const HISTORY_PAGE = 20;

/** Finished sessions, newest first, a page at a time. */
export function useSessions(pageSize = HISTORY_PAGE) {
  return useInfiniteQuery({
    queryKey: [...qk.workout.sessions(), pageSize],
    initialPageParam: 0,
    queryFn: ({ pageParam, signal }) =>
      api.getJson<WorkoutSessionListResponse>(`${BASE}/sessions`, {
        query: { limit: pageSize, offset: pageParam },
        signal,
      }),
    getNextPageParam: (last, pages) => {
      const loaded = pages.reduce((n, p) => n + p.data.length, 0);
      return last.data.length > 0 && loaded < last.total ? loaded : undefined;
    },
  });
}

export function useSessionHeartRate(id: string, enabled: boolean) {
  return useQuery({
    queryKey: qk.workout.heartRate(id),
    queryFn: async ({ signal }) =>
      (
        await api.getJson<{ data: WorkoutHeartRate }>(`${BASE}/sessions/${id}/heart-rate`, {
          signal,
        })
      ).data,
    enabled,
  });
}

export function useReadiness() {
  return useQuery({
    queryKey: qk.workout.readiness(),
    queryFn: async ({ signal }) =>
      (await api.getJson<{ data: WorkoutReadiness }>(`${BASE}/readiness`, { signal })).data,
  });
}

export function useExercise(id: string, enabled: boolean) {
  return useQuery({
    queryKey: qk.workout.exercise(id),
    queryFn: async ({ signal }) =>
      (
        await api.getJson<{ data: Exercise }>(`${BASE}/exercises/${encodeURIComponent(id)}`, {
          signal,
        })
      ).data,
    enabled,
    staleTime: 5 * 60 * 1000,
  });
}

/** [query key, url] of the probe that says whether the module is on. */
export const WORKOUT_PROBE = [qk.workout.enabled(), `${BASE}/sessions/active`] as const;

// ── session mutations ───────────────────────────────────────────────────────

export function useStartSession(onStarted: (session: WorkoutSession) => void) {
  const qc = useQueryClient();
  return useApiMutation(
    async (routineId: string | null) =>
      (
        await api.postJson<{ data: WorkoutSession }>(`${BASE}/sessions`, {
          body: routineId ? { routine_id: routineId } : {},
        })
      ).data,
    {
      onSuccess: (session) => {
        cacheSession(qc, session);
        onStarted(session);
      },
      // 409: a session is already running (another tab or device). Show it.
      onError: () => void qc.invalidateQueries({ queryKey: qk.workout.active() }),
    },
  );
}

export function usePatchSession(id: string, onDone?: (session: WorkoutSession) => void) {
  const qc = useQueryClient();
  return useApiMutation(
    async (body: WorkoutSessionPatch) =>
      (await api.patchJson<{ data: WorkoutSession }>(`${BASE}/sessions/${id}`, { body })).data,
    {
      // A finished session ticks a workout habit and the Habits plate of Now.
      invalidate: [qk.workout.sessions(), qk.workout.heartRate(id), qk.habits.all(), qk.now.all()],
      onSuccess: (session) => {
        cacheSession(qc, session);
        onDone?.(session);
      },
    },
  );
}

export function useDeleteSession(id: string, onDone: () => void) {
  const qc = useQueryClient();
  return useApiMutation(() => api.deleteJson<unknown>(`${BASE}/sessions/${id}`), {
    invalidate: [qk.workout.sessions(), qk.habits.all(), qk.now.all()],
    onSuccess: () => {
      qc.removeQueries({ queryKey: qk.workout.session(id) });
      qc.setQueryData<WorkoutSession | null>(qk.workout.active(), (s) => (s?.id === id ? null : s));
      onDone();
    },
  });
}

export function useAddSessionExercise(sessionId: string, onDone?: (s: WorkoutSession) => void) {
  const qc = useQueryClient();
  return useApiMutation(
    async (exerciseId: string) =>
      (
        await api.postJson<{ data: WorkoutSession }>(`${BASE}/sessions/${sessionId}/exercises`, {
          body: { exercise_id: exerciseId },
        })
      ).data,
    {
      onSuccess: (session) => {
        cacheSession(qc, session);
        onDone?.(session);
      },
    },
  );
}

export function useRemoveSessionExercise(sessionId: string, onDone?: () => void) {
  const qc = useQueryClient();
  return useApiMutation(
    async (sessionExerciseId: string) =>
      (
        await api.deleteJson<{ data: WorkoutSession }>(
          `${BASE}/sessions/${sessionId}/exercises/${sessionExerciseId}`,
        )
      ).data,
    {
      onSuccess: (session) => {
        cacheSession(qc, session);
        onDone?.();
      },
    },
  );
}

// ── sets: the unsent queue ──────────────────────────────────────────────────

/** DELETE one set; already gone (404) is as good as deleted. */
async function deleteSet(id: string) {
  try {
    await api.deleteJson<unknown>(`${BASE}/sets/${id}`);
  } catch (e) {
    if (!(e instanceof ApiClientError && e.status === 404)) throw e;
  }
}

/**
 * Sets are written through a queue kept in localStorage. A set shows up at
 * once and is sent in the background; a failed send is retried with backoff
 * and when the browser comes back online, and survives a reload. The set's id
 * is made on the phone, so a resend replaces the set on the server.
 */
export function useSetQueue(sessionId: string) {
  const qc = useQueryClient();
  const [queue, setQueue] = useState<QueuedSet[]>(() =>
    // Sets the server refused for another session are of no use here.
    readQueue(browserStore()).filter((q) => !(q.rejected && q.sessionId !== sessionId)),
  );
  const queueRef = useRef(queue);
  const sending = useRef(false);
  // The id whose PUT is out now, and the ids deleted while their PUT was out:
  // that PUT may still land, so the set gets a DELETE once it settles.
  const inFlight = useRef<string | null>(null);
  const deletedInFlight = useRef(new Set<string>());
  const timer = useRef<ReturnType<typeof setTimeout> | undefined>(undefined);
  const alive = useRef(true);

  const update = useCallback((change: (q: QueuedSet[]) => QueuedSet[]) => {
    queueRef.current = change(queueRef.current);
    writeQueue(browserStore(), queueRef.current);
    if (alive.current) setQueue(queueRef.current);
  }, []);

  const pump = useCallback(async () => {
    if (sending.current) return;
    const next = queueRef.current.find((q) => !q.rejected);
    if (!next) return;
    sending.current = true;
    inFlight.current = next.id;
    let retryIn: number | null = null;
    try {
      const saved = (
        await api.putJson<{ data: WorkoutSet }>(`${BASE}/sets/${next.id}`, { body: next.body })
      ).data;
      if (deletedInFlight.current.has(next.id)) {
        // Deleted while this PUT was out: the set is on the server now, take it off.
        await deleteSet(next.id).catch(() => undefined);
        deletedInFlight.current.delete(next.id);
        patchCachedSession(qc, next.sessionId, (s) => withoutSet(s, next.id));
        void qc.invalidateQueries({ queryKey: qk.workout.sessions() });
        retryIn = 0;
        return;
      }
      // A refetch that started before the write would bring the session back
      // without this set, after the queue has already let go of it.
      await Promise.all([
        qc.cancelQueries({ queryKey: qk.workout.active() }),
        qc.cancelQueries({ queryKey: qk.workout.session(next.sessionId) }),
      ]);
      // An edit made while this version was in flight stays queued.
      update((q) =>
        q.find((x) => x.id === next.id)?.body === next.body ? dropFromQueue(q, next.id) : q,
      );
      patchCachedSession(qc, next.sessionId, (s) => withSet(s, saved));
      retryIn = 0;
    } catch (e) {
      const status = e instanceof ApiClientError ? e.status : 0;
      if (deletedInFlight.current.delete(next.id)) {
        // A lost answer may still mean a stored set; a refused one means none.
        if (!isPermanentFailure(status)) await deleteSet(next.id).catch(() => undefined);
        retryIn = 0;
      } else if (isPermanentFailure(status)) {
        update((q) => markFailed(q, next.id, apiErrorMessage(e, 'The server refused this set.')));
        retryIn = 0;
      } else {
        update((q) => markFailed(q, next.id));
        retryIn = backoffMs(next.attempts + 1);
      }
    } finally {
      sending.current = false;
      inFlight.current = null;
      if (alive.current) {
        clearTimeout(timer.current);
        if (retryIn === 0) void pump();
        else if (retryIn !== null) timer.current = setTimeout(() => void pump(), retryIn);
      }
    }
  }, [qc, update]);

  useEffect(() => {
    alive.current = true;
    const kick = () => {
      clearTimeout(timer.current);
      void pump();
    };
    kick();
    window.addEventListener('online', kick);
    return () => {
      alive.current = false;
      clearTimeout(timer.current);
      window.removeEventListener('online', kick);
    };
  }, [pump]);

  /** Create the set with this id, or replace it. */
  const put = useCallback(
    (id: string, body: WorkoutSetInput) => {
      update((q) => enqueue(q, { id, sessionId, body }));
      clearTimeout(timer.current);
      void pump();
    },
    [pump, sessionId, update],
  );

  /**
   * Delete a set: an unsent one just leaves the queue; one whose PUT is out
   * gets its DELETE when that PUT settles.
   */
  const remove = useCallback(
    async (id: string, onServer: boolean) => {
      update((q) => dropFromQueue(q, id));
      if (inFlight.current === id) {
        deletedInFlight.current.add(id);
        return;
      }
      if (!onServer) return;
      await deleteSet(id);
      patchCachedSession(qc, sessionId, (s) => withoutSet(s, id));
      void qc.invalidateQueries({ queryKey: qk.workout.sessions() });
    },
    [qc, sessionId, update],
  );

  return { queue, put, remove };
}

// ── rest timer ──────────────────────────────────────────────────────────────

let audio: AudioContext | null = null;

/** Called from a tap, so the browser lets the page make a sound later. */
function armSound() {
  try {
    audio ??= new AudioContext();
    if (audio.state === 'suspended') void audio.resume();
  } catch {
    audio = null;
  }
}

function beep() {
  if (!audio) return;
  try {
    const t = audio.currentTime;
    for (const at of [0, 0.25, 0.5]) {
      const osc = audio.createOscillator();
      const gain = audio.createGain();
      osc.frequency.value = 880;
      gain.gain.setValueAtTime(0.25, t + at);
      gain.gain.exponentialRampToValueAtTime(0.001, t + at + 0.18);
      osc.connect(gain).connect(audio.destination);
      osc.start(t + at);
      osc.stop(t + at + 0.2);
    }
  } catch {
    // No sound is not worth an error.
  }
}

// The rest end lives outside React: the page starts it from a tap, and only
// RestBar, which ticks, subscribes. The page itself does not re-render 4 times
// a second during rest.
const restListeners = new Set<() => void>();
let restEnd: number | null | undefined; // undefined until read from storage

function getRestEnd(): number | null {
  if (restEnd === undefined) restEnd = readRestEnd(browserStore(), Date.now());
  return restEnd;
}

function setRestEnd(value: number | null) {
  writeRestEnd(browserStore(), value);
  restEnd = value;
  restListeners.forEach((f) => f());
}

function subscribeRest(f: () => void) {
  restListeners.add(f);
  return () => {
    restListeners.delete(f);
  };
}

/** Start the rest countdown (0 stops it). Called from a tap, so a sound may follow. */
export function startRest(seconds: number) {
  armSound();
  setRestEnd(seconds > 0 ? Date.now() + seconds * 1000 : null);
}

/** Stop the rest countdown (finish, discard). */
export function stopRest() {
  setRestEnd(null);
}

/**
 * The rest countdown, for the component that shows it. The end time is
 * stored, so a reload keeps the countdown. When it ends the page beeps and
 * `justEnded` is true for a moment (the visual flash).
 */
export function useRestTimer() {
  const endsAt = useSyncExternalStore(subscribeRest, getRestEnd);
  const [now, setNow] = useState(() => Date.now());
  const [justEnded, setJustEnded] = useState(false);

  useEffect(() => {
    if (endsAt === null) return;
    setJustEnded(false);
    setNow(Date.now());
    const tick = setInterval(() => setNow(Date.now()), 250);
    return () => clearInterval(tick);
  }, [endsAt]);

  const remaining = restRemaining(endsAt, now);

  useEffect(() => {
    if (endsAt === null || remaining > 0) return;
    setRestEnd(null);
    beep();
    navigator.vibrate?.([200, 100, 200]);
    setJustEnded(true);
  }, [endsAt, remaining]);

  useEffect(() => {
    if (!justEnded) return;
    const off = setTimeout(() => setJustEnded(false), 4000);
    return () => clearTimeout(off);
  }, [justEnded]);

  return {
    running: endsAt !== null && remaining > 0,
    remaining,
    justEnded,
    add: (seconds: number) => {
      if (endsAt !== null) setRestEnd(endsAt + seconds * 1000);
    },
    stop: () => setRestEnd(null),
  };
}

/** The current time, refreshed every `ms`; for the elapsed-time clock. */
export function useNow(ms: number): number {
  const [now, setNow] = useState(() => Date.now());
  useEffect(() => {
    const tick = setInterval(() => setNow(Date.now()), ms);
    return () => clearInterval(tick);
  }, [ms]);
  return now;
}

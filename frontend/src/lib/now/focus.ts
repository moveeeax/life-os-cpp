// A focus session of Now: only an end time (or the seconds left while paused)
// is kept, per user in the browser, so a reload or a new tab carries on.

export const FOCUS_LENGTHS = [25, 50, 90] as const;
export const FADE_MS = 3000;
/** A finished session shows Done this long, then the screen is Now again. */
export const DONE_KEEP_MS = 60 * 60_000;

export interface Session {
  /** When the session ends, while running; null while paused. */
  endAt: number | null;
  /** Seconds left, while paused; null while running. */
  pausedLeft: number | null;
  minutes: number;
  /** The day of the focus this session worked on; Tick only applies to it. */
  focusDate?: string | null;
  /** When it started (ms); the day ribbon draws it from here. */
  startedAt?: number;
  /** Sent to the server already (POST /now/focus-sessions), so a reload does not send it twice. */
  recorded?: boolean;
}

export type Phase = 'idle' | 'running' | 'paused' | 'done';

export function focusKey(userId: string): string {
  return `now:focus:${userId}`;
}

/**
 * The session another tab wrote, from a `storage` event: null when it ended or
 * the storage was cleared, undefined when the event is not about @p key or
 * its value cannot be read.
 */
export function sessionFromStorage(
  e: { key: string | null; newValue: string | null },
  key: string,
): Session | null | undefined {
  if (e.key === null) return null;
  if (e.key !== key) return undefined;
  if (e.newValue === null) return null;
  try {
    return JSON.parse(e.newValue) as Session | null;
  } catch {
    return undefined;
  }
}

export function startSession(
  minutes: number,
  now: number,
  focusDate: string | null = null,
): Session {
  return { endAt: now + minutes * 60_000, pausedLeft: null, minutes, focusDate, startedAt: now };
}

/** A session cut short this early is not kept. */
export const FOCUS_RECORD_MIN_MINUTES = 5;

/**
 * What the server keeps of @p s at @p now, ended (done) or left early: its
 * start, its end and the minutes in focus (pauses left out); null when it is
 * kept already, too short, or has no start.
 */
export function focusRecord(
  s: Session | null,
  now: number,
): { started_at: string; ended_at: string; minutes: number } | null {
  if (!s || s.recorded || !s.startedAt) return null;
  const state = sessionState(s, now);
  const end = state.phase === 'done' && s.endAt !== null ? s.endAt : now;
  const focused = Math.round((s.minutes * 60 - (state.phase === 'done' ? 0 : state.left)) / 60);
  if (focused < FOCUS_RECORD_MIN_MINUTES || end <= s.startedAt) return null;
  return {
    started_at: new Date(s.startedAt).toISOString().replace(/\.\d{3}Z$/, 'Z'),
    ended_at: new Date(end).toISOString().replace(/\.\d{3}Z$/, 'Z'),
    minutes: Math.min(focused, Math.ceil((end - s.startedAt) / 60_000)),
  };
}

export function sessionState(
  s: Session | null,
  now: number,
): { phase: Phase; left: number; minutes: number } {
  if (!s) return { phase: 'idle', left: 0, minutes: 0 };
  if (s.pausedLeft !== null)
    return { phase: 'paused', left: Math.max(0, s.pausedLeft), minutes: s.minutes };
  if (s.endAt === null) return { phase: 'idle', left: 0, minutes: s.minutes };
  const left = Math.ceil((s.endAt - now) / 1000);
  if (left > 0) return { phase: 'running', left, minutes: s.minutes };
  if (now - s.endAt > DONE_KEEP_MS) return { phase: 'idle', left: 0, minutes: s.minutes };
  return { phase: 'done', left: 0, minutes: s.minutes };
}

export function pauseSession(s: Session, now: number): Session {
  return { ...s, endAt: null, pausedLeft: sessionState(s, now).left };
}

export function resumeSession(s: Session, now: number): Session {
  return { ...s, endAt: now + (s.pausedLeft ?? 0) * 1000, pausedLeft: null };
}

/** The volume @p elapsedMs into a fade towards @p target ('in') or from it to silence ('out'). */
export function fadeVolume(
  target: number,
  elapsedMs: number,
  durationMs: number,
  direction: 'in' | 'out',
): number {
  const share = Math.max(0, Math.min(1, elapsedMs / durationMs));
  return direction === 'in' ? target * share : target * (1 - share);
}

/** mm:ss, or h:mm:ss from an hour up. */
export function clockOf(seconds: number): string {
  const h = Math.floor(seconds / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  const s = seconds % 60;
  const two = (n: number) => String(n).padStart(2, '0');
  return h > 0 ? `${h}:${two(m)}:${two(s)}` : `${two(m)}:${two(s)}`;
}

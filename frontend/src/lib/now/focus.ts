// A focus session of Now: only an end time (or the seconds left while paused)
// is kept, per user in the browser, so a reload or a new tab carries on.

export const FOCUS_LENGTHS = [25, 50, 90] as const;
export const FADE_MS = 3000;

export interface Session {
  /** When the session ends, while running; null while paused. */
  endAt: number | null;
  /** Seconds left, while paused; null while running. */
  pausedLeft: number | null;
  minutes: number;
}

export type Phase = 'idle' | 'running' | 'paused' | 'done';

export function focusKey(userId: string): string {
  return `now:focus:${userId}`;
}

export function startSession(minutes: number, now: number): Session {
  return { endAt: now + minutes * 60_000, pausedLeft: null, minutes };
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
  return left > 0
    ? { phase: 'running', left, minutes: s.minutes }
    : { phase: 'done', left: 0, minutes: s.minutes };
}

export function pauseSession(s: Session, now: number): Session {
  return { endAt: null, pausedLeft: sessionState(s, now).left, minutes: s.minutes };
}

export function resumeSession(s: Session, now: number): Session {
  return { endAt: now + (s.pausedLeft ?? 0) * 1000, pausedLeft: null, minutes: s.minutes };
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

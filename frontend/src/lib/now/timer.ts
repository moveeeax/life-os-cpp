// The 25-minute focus timer of Now: only an end time is kept (per user, in the
// browser), so a new tab shows the same time left.

export const FOCUS_MINUTES = 25;

export interface TimerState {
  running: boolean;
  /** Seconds left, rounded up; 0 when idle or done. */
  left: number;
  done: boolean;
}

export function timerKey(userId: string): string {
  return `now:timer:${userId}`;
}

export function timerState(endAt: number | null, now: number): TimerState {
  if (endAt === null) return { running: false, left: 0, done: false };
  const left = Math.ceil((endAt - now) / 1000);
  return left > 0 ? { running: true, left, done: false } : { running: false, left: 0, done: true };
}

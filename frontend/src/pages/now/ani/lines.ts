import type { NowDay } from '../ribbon/layout';

/** The kinds of her own lines the person may put off ("Later") or mute ("Not about this"). */
export type LineKind =
  | 'brief'
  | 'evening'
  | 'break'
  | 'stretch'
  | 'hello'
  | 'telegram'
  | 'webhook'
  | 'reminder'
  | 'calendar';

/** The browser's choice of hold to talk (Account → Ani): "1" holds. */
export const HOLD_KEY = 'now:ani:hold';

/** Her mutable kinds of lines, as the page names them. */
export const LINE_KINDS: Record<string, string> = {
  brief: 'Morning brief',
  evening: 'Evening word',
  break: 'Break after focus',
  stretch: 'Stretch reminders',
  hello: 'Hellos',
  telegram: 'Telegram news',
  webhook: 'Webhook events',
  reminder: 'Reminders',
  calendar: 'Calendar news',
};

export const SNOOZE_MS = 60 * 60_000;
const snoozeKey = (kind: LineKind) => `now:ani:snooze:${kind}`;

/** Puts a kind off for an hour in this browser. */
export function snooze(kind: LineKind, now = Date.now()): void {
  try {
    localStorage.setItem(snoozeKey(kind), String(now + SNOOZE_MS));
  } catch {
    // A convenience of this browser.
  }
}

/** Whether a line of @p kind keeps quiet now: muted on the server or put off here. */
export function quietKind(
  kind: LineKind | undefined,
  muted: readonly string[],
  now = Date.now(),
): boolean {
  if (!kind) return false;
  if (muted.includes(kind)) return true;
  try {
    const until = Number(localStorage.getItem(snoozeKey(kind)));
    return Number.isFinite(until) && until > now;
  } catch {
    return false;
  }
}

/** The kind of a line that came through her inbox. */
export function inboxKind(kind: string, source: unknown): LineKind {
  if (kind === 'remind') return 'reminder';
  return source === 'telegram' ? 'telegram' : 'webhook';
}

/**
 * The calendar event a spoken line is about, to point at it on the ribbon:
 * its title in the line, or a time "11", "в 11", "at 11:30" that is its start.
 */
export function eventOfLine(text: string, day: NowDay | undefined, tz: string): string | null {
  const events = (day?.events ?? []).filter((e) => !e.all_day);
  if (events.length === 0) return null;
  const low = text.toLowerCase();
  for (const e of events) {
    const title = e.title.trim().toLowerCase();
    if (title.length >= 3 && low.includes(title)) return e.id;
  }
  const times = [...text.matchAll(/(?:^|[^\d:])(\d{1,2})(?::(\d{2}))?(?![\d:])/g)].map(
    (m) => Number(m[1]) * 60 + Number(m[2] ?? 0),
  );
  if (times.length === 0) return null;
  const startOf = (iso: string) => {
    const p = new Intl.DateTimeFormat('en-GB', {
      timeZone: tz,
      hour: '2-digit',
      minute: '2-digit',
      hourCycle: 'h23',
    })
      .format(new Date(iso))
      .split(':')
      .map(Number);
    return p[0] * 60 + p[1];
  };
  return events.find((e) => times.includes(startOf(e.start)))?.id ?? null;
}

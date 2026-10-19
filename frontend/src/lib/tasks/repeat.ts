import type { components } from '@/lib/api/schema.gen';

export type TaskRepeat = components['schemas']['TaskRepeat'];
export type RepeatKind = TaskRepeat['kind'];

/** The repeat fields of the task form; kind '' is "does not repeat". */
export interface RepeatDraft {
  kind: RepeatKind | '';
  weekdays: number[];
  day: string;
  every: string;
}

export const WEEKDAYS: { n: number; short: string }[] = [
  { n: 1, short: 'Mon' },
  { n: 2, short: 'Tue' },
  { n: 3, short: 'Wed' },
  { n: 4, short: 'Thu' },
  { n: 5, short: 'Fri' },
  { n: 6, short: 'Sat' },
  { n: 7, short: 'Sun' },
];

/** The ISO weekday (1 is Monday) of a YYYY-MM-DD day. */
export function isoWeekday(day: string): number {
  const d = new Date(`${day}T00:00:00Z`).getUTCDay();
  return d === 0 ? 7 : d;
}

/** The form's start: the task's rule, or defaults taken from its due date. */
export function repeatDraft(rule: TaskRepeat | null | undefined, due?: string | null): RepeatDraft {
  const fromDue = due
    ? { weekdays: [isoWeekday(due)], day: String(Number(due.slice(8, 10))) }
    : null;
  return {
    kind: rule?.kind ?? '',
    weekdays: rule?.weekdays ?? fromDue?.weekdays ?? [1],
    day: String(rule?.day ?? fromDue?.day ?? 1),
    every: String(rule?.every ?? 2),
  };
}

/** The rule the draft names, or null; undefined when the draft is not a rule yet (no weekday, a bad number). */
export function repeatOf(d: RepeatDraft): TaskRepeat | null | undefined {
  if (d.kind === '') return null;
  if (d.kind === 'daily') return { kind: 'daily' };
  if (d.kind === 'weekly') {
    const days = [...new Set(d.weekdays)].filter((n) => n >= 1 && n <= 7).sort((a, b) => a - b);
    return days.length > 0 ? { kind: 'weekly', weekdays: days } : undefined;
  }
  const whole = (v: string, lo: number, hi: number) => {
    const n = Number(v);
    return Number.isInteger(n) && n >= lo && n <= hi ? n : null;
  };
  if (d.kind === 'monthly') {
    const day = whole(d.day, 1, 31);
    return day === null ? undefined : { kind: 'monthly', day };
  }
  const every = whole(d.every, 1, 365);
  return every === null ? undefined : { kind: 'every_n_days', every };
}

/** Whether two rules are the same (the stored form keeps weekdays sorted). */
export function sameRepeat(a: TaskRepeat | null | undefined, b: TaskRepeat | null | undefined) {
  return JSON.stringify(a ?? null) === JSON.stringify(b ?? null);
}

/** The rule in words: "every day", "every Mon, Thu", "monthly on the 31st", "every 3 days". */
export function repeatLabel(rule: TaskRepeat | null | undefined): string | null {
  if (!rule) return null;
  switch (rule.kind) {
    case 'daily':
      return 'every day';
    case 'weekly':
      return `every ${(rule.weekdays ?? [])
        .map((n) => WEEKDAYS.find((w) => w.n === n)?.short ?? '')
        .join(', ')}`;
    case 'monthly':
      return `monthly on the ${ordinal(rule.day ?? 1)}`;
    case 'every_n_days':
      return rule.every === 1 ? 'every day' : `every ${rule.every} days`;
  }
}

function ordinal(n: number): string {
  const tens = n % 100;
  if (tens >= 11 && tens <= 13) return `${n}th`;
  return `${n}${{ 1: 'st', 2: 'nd', 3: 'rd' }[n % 10] ?? 'th'}`;
}

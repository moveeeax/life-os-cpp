// The four corners of Now: one main line and one small line each, a dot when
// something is off (the rules of the quiet lines), and the countdowns of the
// Goals corner.
import { daysBetween } from '@/lib/dates';
import { paceWord, type Tone } from '@/lib/goals';

import { quietLines, type LinePart } from './lines';
import type { Today } from './types';

export interface Chip {
  key: 'body' | 'goals' | 'day' | 'tasks' | 'habits';
  title: string;
  main: LinePart[];
  small: LinePart[];
  dot: null | 'warn' | 'late';
}

export interface Countdown {
  id: string;
  title: string;
  due: string;
  days: number;
  /** "today" | "tomorrow" | "in N days" */
  words: string;
}

export const COUNTDOWN_DAYS = 60;

const n0 = new Intl.NumberFormat('en-US', { maximumFractionDigits: 0 });
const n1 = new Intl.NumberFormat('en-US', { maximumFractionDigits: 1, minimumFractionDigits: 1 });

const money0 = new Intl.NumberFormat('en-US', { maximumFractionDigits: 0 });
const money2 = new Intl.NumberFormat('en-US', {
  minimumFractionDigits: 2,
  maximumFractionDigits: 2,
});

/** An amount in its own currency: whole numbers stay whole, cents show as two digits. */
export function formatMoney(amount: number): string {
  return Number.isInteger(Math.round(amount * 100) / 100)
    ? money0.format(amount)
    : money2.format(amount);
}

/** The big figure of a countdown row: none for today and tomorrow, where the words say it. */
export function countdownFigure(days: number): { figure: string | null; words: string } {
  if (days === 0) return { figure: null, words: 'today' };
  if (days === 1) return { figure: null, words: 'tomorrow' };
  return { figure: String(days), words: 'days left' };
}

/** 412 → "6 h 52"; under an hour → "45 min". */
export function formatSleep(minutes: number): string {
  const m = Math.round(minutes);
  if (m < 60) return `${m} min`;
  return `${Math.floor(m / 60)} h ${String(m % 60).padStart(2, '0')}`;
}

function inDays(days: number): string {
  if (days === 0) return 'today';
  if (days === 1) return 'tomorrow';
  return `in ${days} days`;
}

/** Goals due within @p horizonDays of @p todayDate (local), nearest first; past ones left out. */
export function countdowns(
  today: Today,
  todayDate: string,
  horizonDays = COUNTDOWN_DAYS,
): Countdown[] {
  const trip = today.travel?.next_trip;
  const trips = trip
    ? [
        {
          id: `trip:${trip.id}`,
          title: trip.name,
          due: trip.start_date,
          days: daysBetween(todayDate, trip.start_date),
        },
      ]
    : [];
  return [
    ...(today.goals ?? []).map((g) => ({
      id: g.id,
      title: g.title,
      due: g.due,
      days: daysBetween(todayDate, g.due),
    })),
    ...trips,
  ]
    .filter((c) => c.days >= 0 && c.days <= horizonDays)
    .sort((a, b) => a.days - b.days || a.title.localeCompare(b.title))
    .map((c) => ({ ...c, words: inDays(c.days) }));
}

/** Joins non-empty groups of parts with " · ". */
function joined(groups: LinePart[][]): LinePart[] {
  const out: LinePart[] = [];
  for (const g of groups.filter((x) => x.length > 0)) {
    if (out.length) out.push({ text: ' · ' });
    out.push(...g);
  }
  return out;
}

function bodyChip(t: Today): Chip {
  const h = t.health;
  const sleep: LinePart[] = h?.sleep
    ? [{ text: formatSleep(h.sleep.minutes), num: true }, { text: ' sleep' }]
    : [];
  const steps: LinePart[] =
    h?.steps != null ? [{ text: n0.format(h.steps), num: true }, { text: ' steps' }] : [];
  const weight: LinePart[] =
    h?.weight_kg != null ? [{ text: n1.format(h.weight_kg), num: true }, { text: ' kg' }] : [];
  const d = h?.weight_change_7d;
  const change: LinePart[] =
    d != null
      ? [
          { text: `${d > 0 ? '+' : d < 0 ? '−' : '±'}${n1.format(Math.abs(d))}`, num: true },
          { text: ' this week' },
        ]
      : [];
  const activity = joined([sleep, steps]);
  if (activity.length) {
    // Sleep and steps on top; the weight and its week below.
    const small = weight.length
      ? [...weight, ...(change.length ? [{ text: ', ' }, ...change] : [])]
      : [];
    return { key: 'body', title: 'Body', main: activity, small, dot: null };
  }
  if (weight.length) return { key: 'body', title: 'Body', main: weight, small: change, dot: null };
  return {
    key: 'body',
    title: 'Body',
    main: [{ text: 'No data from the band today' }],
    small: [],
    dot: null,
  };
}

function goalsChip(t: Today, todayDate: string): Chip {
  const next = countdowns(t, todayDate)[0];
  const behind = (t.goals ?? []).find((g) => g.pace === 'behind');
  const main: LinePart[] = next
    ? next.days <= 1
      ? [{ text: `${next.title} ${next.words}` }]
      : [{ text: `${next.title} in ` }, { text: String(next.days), num: true }, { text: ' days' }]
    : [{ text: 'No goal due soon' }];
  const near = t.travel?.near?.[0];
  const small: LinePart[] = near
    ? [{ text: `${near.rule}: ` }, { text: String(near.left), num: true }, { text: ' days left' }]
    : behind
      ? [{ text: `${behind.title} is behind` }]
      : [{ text: 'All on track' }];
  return { key: 'goals', title: 'Goals', main, small, dot: behind || near ? 'warn' : null };
}

function dayChip(t: Today, hour: number): Chip {
  const f = t.food;
  let main: LinePart[] = [{ text: 'Nothing eaten yet' }];
  if (f?.target && f.target.protein_g != null) {
    const left = Math.round(f.target.protein_g - f.eaten.protein_g);
    main =
      left > 0
        ? [{ text: String(left), num: true }, { text: ' g protein left' }]
        : [{ text: 'Protein reached' }];
  } else if (f && f.eaten.kcal > 0) {
    main = [{ text: n0.format(f.eaten.kcal), num: true }, { text: ' kcal eaten' }];
  }
  const first = (t.money?.currencies ?? []).find((c) => c.today > 0);
  const small: LinePart[] = first
    ? [{ text: formatMoney(first.today), num: true }, { text: ` ${first.currency} spent today` }]
    : t.money?.net_worth
      ? [
          { text: 'All accounts ' },
          { text: n0.format(t.money.net_worth.total), num: true },
          { text: ` ${t.money.net_worth.currency}` },
          ...(t.money.net_worth.partial ? [{ text: ' · partial' }] : []),
        ]
      : [{ text: 'Nothing spent today' }];
  const off = quietLines(t, hour).some((l) => l.key === 'protein' || l.key === 'money');
  return { key: 'day', title: 'Food and money', main, small, dot: off ? 'warn' : null };
}

function tasksChip(t: Today): Chip {
  const k = t.tasks;
  const open = (k?.today ?? []).filter((x) => x.status !== 'done');
  const late = k?.late_count ?? 0;
  const parts: LinePart[][] = [];
  if (open.length) parts.push([{ text: String(open.length), num: true }, { text: ' today' }]);
  if (late) parts.push([{ text: String(late), num: true }, { text: ' late' }]);
  const main = parts.length ? joined(parts) : [{ text: 'Nothing for today' }];
  const small: LinePart[] = open.length
    ? [{ text: `Next: ${open[0].title}`, title: true }]
    : late && k?.first_late
      ? [{ text: `Oldest: ${k.first_late.title}` }]
      : [{ text: 'Nothing late' }];
  return { key: 'tasks', title: 'Tasks', main, small, dot: late ? 'late' : null };
}

/** The hour from which an open daily habit lights the dot of the Habits plate. */
export const HABITS_EVENING_HOUR = 20;

function habitsChip(t: Today, hour: number): Chip {
  const h = t.habits;
  if (!h || h.total === 0) {
    return {
      key: 'habits',
      title: 'Habits',
      main: [{ text: 'No habits yet' }],
      small: [],
      dot: null,
    };
  }
  const main: LinePart[] = [
    { text: String(h.done), num: true },
    { text: ' of ' },
    { text: String(h.total), num: true },
    { text: ' today' },
  ];
  const small: LinePart[] = h.left.length
    ? [{ text: `${h.left.join(', ')} left`, title: true }]
    : [{ text: h.done >= h.total ? 'All done today' : 'Every day done' }];
  return {
    key: 'habits',
    title: 'Habits',
    main,
    small,
    dot: h.left.length > 0 && hour >= HABITS_EVENING_HOUR ? 'warn' : null,
  };
}

/** Every chip in the default order: the four corners, then Habits (placed through Arrange). */
export function chips(today: Today, hour: number, todayDate: string): Chip[] {
  return [
    bodyChip(today),
    goalsChip(today, todayDate),
    dayChip(today, hour),
    tasksChip(today),
    habitsChip(today, hour),
  ];
}

/** The pace word of a goal in the digest: the same words as the Goals page. */
export function goalPace(g: NonNullable<Today['goals']>[number]): { text: string; tone: Tone } {
  return paceWord({ kind: g.kind, unit: g.unit, progress: g.detail });
}

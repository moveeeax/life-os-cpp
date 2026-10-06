// Pure logic of the Food pages: dates, scaling, the parse draft, formats.
import type { FoodEntryInput, FoodItem, FoodMeal, FoodParseJob, FoodWeek } from './types';

export * from './types';

export const MEALS: { value: FoodMeal; label: string }[] = [
  { value: 'breakfast', label: 'Breakfast' },
  { value: 'lunch', label: 'Lunch' },
  { value: 'dinner', label: 'Dinner' },
  { value: 'snack', label: 'Snack' },
];

export const mealLabel = (meal: FoodMeal): string =>
  MEALS.find((m) => m.value === meal)?.label ?? meal;

// ── dates (calendar days in the browser's zone, "YYYY-MM-DD" on the wire) ──

const pad = (n: number) => String(n).padStart(2, '0');

const fromParts = (d: Date): string =>
  `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}`;

/** Local date as the API expects it. */
export const todayLocal = (now: Date = new Date()): string => fromParts(now);

const DATE_RE = /^(\d{4})-(\d{2})-(\d{2})$/;

/** Parse a "YYYY-MM-DD" into a local Date, or null when it is not a real date. */
export function toLocalDate(value: string): Date | null {
  const m = DATE_RE.exec(value);
  if (!m) return null;
  const [y, mo, d] = [Number(m[1]), Number(m[2]), Number(m[3])];
  const date = new Date(y, mo - 1, d);
  return date.getFullYear() === y && date.getMonth() === mo - 1 && date.getDate() === d
    ? date
    : null;
}

/** A date from the URL: the value when it is a real date, else the fallback. */
export const parseDateParam = (value: string | null, fallback: string): string =>
  value !== null && toLocalDate(value) !== null ? value : fallback;

export function shiftDate(date: string, days: number): string {
  const d = toLocalDate(date) ?? new Date();
  d.setDate(d.getDate() + days);
  return fromParts(d);
}

/** The Monday of the week the date is in. */
export function mondayOf(date: string): string {
  const d = toLocalDate(date) ?? new Date();
  const back = (d.getDay() + 6) % 7;
  d.setDate(d.getDate() - back);
  return fromParts(d);
}

/** "Mon, Oct 5" for a "YYYY-MM-DD". */
export function formatDate(date: string): string {
  const d = toLocalDate(date);
  return d
    ? d.toLocaleDateString('en-US', { weekday: 'short', month: 'short', day: 'numeric' })
    : date;
}

/** "Mon" for a "YYYY-MM-DD". */
export function weekdayOf(date: string): string {
  const d = toLocalDate(date);
  return d ? d.toLocaleDateString('en-US', { weekday: 'short' }) : date;
}

// ── numbers ────────────────────────────────────────────────────────────────

const round1 = (n: number) => Math.round(n * 10) / 10;

export interface Macros {
  kcal: number;
  protein_g: number;
  fat_g: number;
  carbs_g: number;
}

/** Nutrients of `grams` of an item whose numbers are per 100 g / 100 ml. */
export function scaled(
  item: Pick<FoodItem, 'kcal' | 'protein_g' | 'fat_g' | 'carbs_g'>,
  grams: number,
): Macros {
  const f = grams / 100;
  return {
    kcal: Math.round(item.kcal * f),
    protein_g: round1(item.protein_g * f),
    fat_g: round1(item.fat_g * f),
    carbs_g: round1(item.carbs_g * f),
  };
}

/** Share of the target in percent (may exceed 100); null without a target. */
export const percent = (value: number, target: number | null | undefined): number | null =>
  target ? Math.round((value / target) * 100) : null;

export const formatKcal = (n: number): string => Math.round(n).toLocaleString('en-US');

export const formatGrams = (n: number): string => String(round1(n));

/** The number a field holds, or null when it is empty or not a number. */
export function parseNumber(value: string): number | null {
  const t = value.trim().replace(',', '.');
  if (t === '') return null;
  const n = Number(t);
  return Number.isFinite(n) ? n : null;
}

// ── the parse draft ────────────────────────────────────────────────────────

export const DRAFT_NUMBERS = ['grams', 'kcal', 'protein_g', 'fat_g', 'carbs_g'] as const;
export type DraftNumber = (typeof DRAFT_NUMBERS)[number];

/**
 * One editable line of the parse result. The numbers are the field texts, so
 * a half-typed "12." or an emptied field survives a keystroke; they are
 * parsed when the lines become entries.
 */
export interface ParseDraftLine {
  key: string;
  selected: boolean;
  name: string;
  note: string;
  item_id: string | null;
  estimated: boolean;
  grams: string;
  kcal: string;
  protein_g: string;
  fat_g: string;
  carbs_g: string;
}

/** The job's lines as the editable draft: every line selected. */
export const draftFromJob = (job: FoodParseJob): ParseDraftLine[] =>
  (job.result ?? []).map((l, i) => ({
    key: `${job.id}:${i}`,
    selected: true,
    name: l.name,
    note: l.note,
    item_id: l.item_id,
    estimated: l.estimated,
    grams: String(l.grams),
    kcal: String(l.kcal),
    protein_g: String(l.protein_g),
    fat_g: String(l.fat_g),
    carbs_g: String(l.carbs_g),
  }));

/** The first thing wrong with the selected lines, or null when they can be added. */
export function draftProblem(lines: ParseDraftLine[]): string | null {
  for (const l of lines.filter((x) => x.selected)) {
    const who = l.name.trim() || 'a line';
    if (!l.name.trim()) return 'Every line needs a name.';
    const grams = parseNumber(l.grams);
    if (grams === null || grams <= 0) return `${who}: grams must be above 0.`;
    for (const key of DRAFT_NUMBERS.slice(1)) {
      const n = parseNumber(l[key]);
      if (n === null || n < 0)
        return `${who}: ${key === 'kcal' ? 'kcal' : key.replace('_g', '')} must be 0 or more.`;
    }
  }
  return null;
}

/** The selected lines as entries of the batch route; call draftProblem first. */
export function linesToEntries(
  lines: ParseDraftLine[],
  date: string,
  meal: FoodMeal,
): FoodEntryInput[] {
  const n = (v: string) => parseNumber(v) ?? 0;
  return lines
    .filter((l) => l.selected)
    .map((l) =>
      l.item_id
        ? { date, meal, item_id: l.item_id, name: l.name.trim(), grams: n(l.grams), note: l.note }
        : {
            date,
            meal,
            name: l.name.trim(),
            grams: n(l.grams),
            kcal: n(l.kcal),
            protein_g: n(l.protein_g),
            fat_g: n(l.fat_g),
            carbs_g: n(l.carbs_g),
            note: l.note,
          },
    );
}

/** A readable sentence for the job's `error` ("<code>: <detail>"). */
export function parseErrorText(error: string | null | undefined): string {
  if (!error) return 'The parse failed.';
  const colon = error.indexOf(':');
  const code = colon === -1 ? error : error.slice(0, colon);
  const detail = colon === -1 ? '' : error.slice(colon + 1).trim();
  if (code === 'invalid_answer')
    return `The model did not answer with a usable list${detail ? `: ${detail}` : '.'}`;
  if (code === 'not_configured') return 'Text parsing is not configured on the server.';
  if (code === 'food_disabled') return 'The food module is switched off on the server.';
  if (code === 'provider_refused') return 'The language model provider refused the request.';
  if (code === 'provider_unavailable')
    return 'The language model provider did not answer after several tries. Try again later.';
  if (code === 'parse_timeout') return 'The parse is taking too long. Try again in a minute.';
  const status = /^provider_error_(\d+)$/.exec(code);
  if (status)
    return `The language model provider answered with an error (${status[1]})${detail ? `: ${detail}` : '.'}`;
  return detail || code;
}

// ── the week ───────────────────────────────────────────────────────────────

export function weekSeries(week: FoodWeek): {
  days: string[];
  kcal: number[];
  target: number | null;
} {
  return {
    days: week.days.map((d) => d.date),
    kcal: week.days.map((d) => d.kcal),
    target: week.targets?.kcal ?? null,
  };
}

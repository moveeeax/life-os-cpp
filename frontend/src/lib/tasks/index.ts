// Tasks on the page: areas with their colour, the effort words, the due label.
export * from './types';
export * from './projects';
import { daysBetween, formatDay, isDay, todayLocal } from '@/lib/dates';

import type { Area, Effort } from './types';

export const AREAS: { key: Area; label: string; dot: string }[] = [
  { key: 'finance', label: 'Finance', dot: 'bg-success-500' },
  { key: 'travel', label: 'Travel', dot: 'bg-blue-500' },
  { key: 'health', label: 'Health', dot: 'bg-error-500' },
  { key: 'growth', label: 'Growth', dot: 'bg-violet-500' },
  { key: 'relationships', label: 'Relationships', dot: 'bg-orange-500' },
  { key: 'projects', label: 'Projects', dot: 'bg-gray-400' },
];

export const areaOf = (key: Area) => AREAS.find((a) => a.key === key) ?? AREAS[AREAS.length - 1];

export const EFFORT_LABEL: Record<Effort, string> = {
  '5min': '5 min',
  '30min': '30 min',
  deep: 'deep work',
};

export function dueLabel(
  due: string | null | undefined,
  today: string,
): { text: string; tone: 'late' | 'today' | 'plain' } | null {
  if (!due) return null;
  const d = daysBetween(today, due);
  if (d < 0) return { text: `${-d} ${d === -1 ? 'day' : 'days'} late`, tone: 'late' };
  if (d === 0) return { text: 'today', tone: 'today' };
  if (d === 1) return { text: 'tomorrow', tone: 'plain' };
  return { text: formatDay(due, { locale: 'en-GB' }), tone: 'plain' };
}

/** A due date the API takes: a real day in 1900..2100 (is_calendar_date). */
export function isDueDate(v: string): boolean {
  if (!isDay(v)) return false;
  const y = Number(v.slice(0, 4));
  return y >= 1900 && y <= 2100;
}

/** A source link the page may put in an href: http(s) only, never javascript: or data:. */
export function safeLink(ref: string | null | undefined): string | null {
  if (!ref) return null;
  try {
    const u = new URL(ref);
    return u.protocol === 'https:' || u.protocol === 'http:' ? u.href : null;
  } catch {
    return null;
  }
}

/** What a failed phrase parse says to a person; the raw `code: message` stays out of sight. */
export function parseErrorText(error: string | null | undefined): string {
  const code = (error ?? '').split(':')[0].trim();
  const words: Record<string, string> = {
    not_configured: 'The phrase reader is not set up.',
    invalid_answer: 'The reader answered something that is not a task.',
    provider_unavailable: 'The reader is not answering right now.',
    parse_timeout: 'The reader took too long.',
    queue_unavailable: 'The phrase could not be queued.',
    too_many_parses: 'Three phrases are still being read; wait a moment.',
    refusal: 'The reader declined this phrase.',
  };
  return words[code] ?? 'The phrase could not be read.';
}

/** Today in the browser's zone. */
export const localToday = (): string => todayLocal();

export function localZone(): string {
  return Intl.DateTimeFormat().resolvedOptions().timeZone || 'UTC';
}

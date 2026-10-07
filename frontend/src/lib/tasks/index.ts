// Tasks on the page: areas with their colour, the effort words, the due label.
export * from './types';
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

/** Days from `from` to `to`, both YYYY-MM-DD, counted on the calendar (UTC noon avoids DST). */
const dayGap = (from: string, to: string) =>
  Math.round((Date.parse(`${to}T12:00:00Z`) - Date.parse(`${from}T12:00:00Z`)) / 86_400_000);

export function dueLabel(
  due: string | null | undefined,
  today: string,
): { text: string; tone: 'late' | 'today' | 'plain' } | null {
  if (!due) return null;
  const d = dayGap(today, due);
  if (d < 0) return { text: `${-d} ${d === -1 ? 'day' : 'days'} late`, tone: 'late' };
  if (d === 0) return { text: 'today', tone: 'today' };
  if (d === 1) return { text: 'tomorrow', tone: 'plain' };
  const text = new Date(`${due}T12:00:00Z`).toLocaleDateString('en-GB', {
    day: 'numeric',
    month: 'short',
    timeZone: 'UTC',
  });
  return { text, tone: 'plain' };
}

export function localToday(): string {
  const n = new Date();
  const p = (x: number) => String(x).padStart(2, '0');
  return `${n.getFullYear()}-${p(n.getMonth() + 1)}-${p(n.getDate())}`;
}

export function localZone(): string {
  return Intl.DateTimeFormat().resolvedOptions().timeZone || 'UTC';
}

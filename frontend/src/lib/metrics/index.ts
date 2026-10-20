// The metrics catalog (GET /api/v1/metrics) as the picker shows it: groups by
// section in a fixed order, the reason an entry cannot be read, the option text.
import type { components } from '@/lib/api/schema.gen';

type S = components['schemas'];
export type MetricEntry = S['MetricEntry'];
export type MetricModule = MetricEntry['module'];
export type MetricWhy = NonNullable<S['MetricWhy']>;

/** The sections in the order the picker lists them, with their names. */
export const MODULES: { key: MetricModule; label: string }[] = [
  { key: 'fitness', label: 'Fitness' },
  { key: 'food', label: 'Food' },
  { key: 'habits', label: 'Habits' },
  { key: 'tape', label: 'Tape' },
  { key: 'workout', label: 'Training' },
  { key: 'tasks', label: 'Tasks' },
  { key: 'money', label: 'Money' },
  { key: 'practice', label: 'Practice' },
];

/** Why an entry cannot be read, in a few words. */
export const WHY_WORDS: Record<MetricWhy, string> = {
  module_off: 'section off',
  no_permission: 'no access',
  not_linked: 'not linked',
  not_yours: 'not yours',
};

/** A calm line under a goal whose source gives nothing. */
export const WHY_NOTE: Record<MetricWhy, string> = {
  module_off: 'The section of this source is off, so only check-ins count.',
  no_permission: 'This account cannot read the source, so only check-ins count.',
  not_linked: 'No Mi account is linked, so only check-ins count.',
  not_yours: 'The source is no longer yours, so only check-ins count.',
};

/** "Weight, kg · not linked". */
export function optionText(e: Pick<MetricEntry, 'label' | 'unit' | 'available' | 'why'>): string {
  // Money and practice labels already end with their currency.
  const unit = e.unit && !e.label.endsWith(`, ${e.unit}`) ? `, ${e.unit}` : '';
  const why = !e.available && e.why ? ` · ${WHY_WORDS[e.why]}` : '';
  return `${e.label}${unit}${why}`;
}

export interface MetricGroup {
  key: MetricModule;
  label: string;
  entries: MetricEntry[];
}

/** The entries grouped by section in MODULES order, catalog order inside; empty groups left out. */
export function groupMetrics(
  entries: MetricEntry[],
  exclude: readonly string[] = [],
): MetricGroup[] {
  const skip = new Set(exclude);
  return MODULES.map((m) => ({
    ...m,
    entries: entries.filter((e) => e.module === m.key && !skip.has(e.id)),
  })).filter((g) => g.entries.length > 0);
}

/** The entry of @p id, if the catalog has it. */
export const entryOf = (entries: MetricEntry[] | undefined, id: string | null | undefined) =>
  id ? entries?.find((e) => e.id === id) : undefined;

import type { components } from '@/lib/api/schema.gen';

export type Widget = components['schemas']['NowWidget'];
export const WIDGETS_MAX = 6;

/** The fixed widgets and their names; a series is named by its catalog entry. */
export const FIXED: Record<Exclude<Widget['kind'], 'series'>, string> = {
  calendar_week: 'Calendar week',
  habits: 'Habits today',
  spend: 'Spending this month',
  telegram: 'Telegram',
  trip: 'Next trip',
  water: 'Water today',
  protein: 'Protein today',
  documents: 'Documents',
};

export const widgetKey = (w: Widget) => (w.kind === 'series' ? `series:${w.source}` : w.kind);

// The greeting of Now: the part of the day by the local hour, and the name.

export type PartOfDay = 'morning' | 'afternoon' | 'evening' | 'night';

/** 5–12 morning, 12–18 afternoon, 18–23 evening, 23–5 night. */
export function partOfDay(hour: number): PartOfDay {
  if (hour >= 5 && hour < 12) return 'morning';
  if (hour >= 12 && hour < 18) return 'afternoon';
  if (hour >= 18 && hour < 23) return 'evening';
  return 'night';
}

export function greeting(hour: number, firstName?: string | null): string {
  const name = firstName?.trim();
  const base = `Good ${partOfDay(hour)}`;
  return name ? `${base}, ${name}` : base;
}

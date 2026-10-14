export type * from './types';
export * from './draft';
export * from './markdown';

/** "otel, tracing , otel" → ["otel", "tracing"]: trimmed, no empties, no repeats. */
export function tagsFrom(text: string): string[] {
  const out: string[] = [];
  for (const t of text.split(',')) {
    const tag = t.trim();
    if (tag && !out.includes(tag)) out.push(tag);
  }
  return out;
}

/** The month a note falls in, for the list groups: "October 2026". */
export function monthOf(iso: string): string {
  return new Date(iso).toLocaleDateString('en-GB', {
    month: 'long',
    year: 'numeric',
    timeZone: 'UTC',
  });
}

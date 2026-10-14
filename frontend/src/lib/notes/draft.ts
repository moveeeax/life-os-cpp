import type { Note } from './types';

/** The editor's fields, all as the strings the inputs hold. */
export interface Draft {
  title: string;
  topic: string;
  tags: string;
  summary: string;
  slug: string;
  project: string;
  body: string;
}

const KEYS: readonly (keyof Draft)[] = [
  'title',
  'topic',
  'tags',
  'summary',
  'slug',
  'project',
  'body',
];

export const draftOf = (n: Note): Draft => ({
  title: n.title,
  topic: n.topic,
  tags: n.tags.join(', '),
  summary: n.summary,
  slug: n.slug,
  project: n.project_id ?? '',
  body: n.body ?? '',
});

export const isDirty = (d: Draft, base: Draft): boolean => KEYS.some((k) => d[k] !== base[k]);

/**
 * The draft after a save answered: a field still equal to what was sent takes
 * the server's form (trimmed, normalised tags); a field edited while the save
 * was in flight keeps the newer words.
 */
export function mergeSaved(current: Draft, submitted: Draft, saved: Draft): Draft {
  const out = { ...current };
  for (const k of KEYS) if (current[k] === submitted[k]) out[k] = saved[k];
  return out;
}

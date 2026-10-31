/**
 * The search of the whole site (Cmd+K): what a query matches and where a
 * hit opens. The palette asks the sections that search on the server
 * (tasks, notes, the ledger) and filters the short lists itself (goals,
 * habits, trips, the pages).
 */

export const GROUPS = ['Pages', 'Tasks', 'Notes', 'Money', 'Goals', 'Habits', 'Trips'] as const;
export type Group = (typeof GROUPS)[number];

export interface Hit {
  key: string;
  group: Group;
  title: string;
  /** A day (YYYY-MM-DD) shown at the right of the row. */
  when?: string | null;
  to: string;
}

/** Each group shows this many hits at most. */
export const PER_GROUP = 5;

const fold = (s: string) => s.toLocaleLowerCase().replaceAll('ё', 'е');

/** Whether @p text holds @p q, case-insensitive, ё read as е. */
export function matches(text: string | null | undefined, q: string): boolean {
  return !!text && fold(text).includes(fold(q.trim()));
}

/** The sections of the nav and the account pages whose names hold @p q. */
export function pageHits(q: string, nav: readonly { label: string; to: string }[]): Hit[] {
  const pages = [
    ...nav,
    { label: 'Account', to: '/account' },
    { label: 'Privacy policy', to: '/privacy' },
  ];
  return pages
    .filter((p) => matches(p.label, q))
    .map((p) => ({ key: `page:${p.to}`, group: 'Pages' as const, title: p.label, to: p.to }));
}

/** A task opens on Today when it is due by @p today, on Later otherwise. */
export function taskHit(t: { id: string; title: string; due?: string | null }, today: string): Hit {
  const page = t.due && t.due <= today ? '/tasks' : '/tasks/later';
  return {
    key: `task:${t.id}`,
    group: 'Tasks',
    title: t.title,
    when: t.due ?? null,
    to: `${page}?task=${t.id}`,
  };
}

export function noteHit(n: {
  id: string;
  title: string;
  date?: string | null;
  updated_at: string;
}): Hit {
  return {
    key: `note:${n.id}`,
    group: 'Notes',
    title: n.title || 'Untitled note',
    when: n.date ?? n.updated_at.slice(0, 10),
    to: `/notes/${n.id}`,
  };
}

export function txHit(t: {
  id: string;
  name?: string | null;
  merchant?: string | null;
  date: string;
}): Hit {
  return {
    key: `tx:${t.id}`,
    group: 'Money',
    title: t.name || t.merchant || 'A ledger row',
    when: t.date,
    to: `/money?row=${t.id}`,
  };
}

/** The hits by group, in the fixed order, without empty groups. */
export function grouped(hits: readonly Hit[]): [Group, Hit[]][] {
  return GROUPS.flatMap((g) => {
    const of = hits.filter((h) => h.group === g).slice(0, PER_GROUP);
    return of.length > 0 ? [[g, of] as [Group, Hit[]]] : [];
  });
}

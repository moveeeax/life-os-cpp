import type { TabItem } from '@/shell/kit';

/** The three views as tabs; the counts come from the list's answer. */
export function githubTabs(counts?: {
  attention: number;
  all: number;
  archived: number;
}): TabItem[] {
  return [
    { to: '/github', label: 'Needs attention', count: counts?.attention, end: true },
    { to: '/github/all', label: 'All', count: counts?.all },
    { to: '/github/archived', label: 'Archived', count: counts?.archived },
  ];
}

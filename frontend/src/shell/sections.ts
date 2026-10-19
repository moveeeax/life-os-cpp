import { Permission, userCan, type PermissionUser } from '@/lib/auth/permissions';

/**
 * The glass pill nav: the one list of the signed-in app's sections. A
 * section whose backend module is off is left out (Now too); Body and
 * Training need fitness:read, but Body also shows without it while the
 * medical module is on, opening at the medical records. Routes keep their
 * paths (Body is /health, Training is /workout).
 */
export interface NavItem {
  key: string;
  label: string;
  to: string;
  /** Permission bit the section needs; the item is hidden without it. */
  permission?: number;
  /** A module that shows the item without the permission, linking to `to` instead. */
  without?: { module: string; to: string };
}

export const NAV: readonly NavItem[] = [
  { key: 'now', label: 'Now', to: '/now' },
  { key: 'tasks', label: 'Tasks', to: '/tasks' },
  { key: 'goals', label: 'Goals', to: '/goals' },
  { key: 'habits', label: 'Habits', to: '/habits' },
  { key: 'learning', label: 'Learning', to: '/learning' },
  { key: 'notes', label: 'Notes', to: '/notes' },
  { key: 'practice', label: 'Practice', to: '/practice' },
  { key: 'travel', label: 'Travel', to: '/travel' },
  { key: 'money', label: 'Money', to: '/money' },
  { key: 'food', label: 'Food', to: '/food' },
  {
    key: 'health',
    label: 'Body',
    to: '/health',
    permission: Permission.FitnessRead,
    without: { module: 'medical', to: '/health/records' },
  },
  { key: 'workout', label: 'Training', to: '/workout', permission: Permission.FitnessRead },
];

/**
 * The items this user sees; `off` holds the keys of modules switched off and
 * `on` the keys of modules known to be on.
 */
export function glassNav(
  user: PermissionUser | null | undefined,
  off: ReadonlySet<string> = new Set(),
  on: ReadonlySet<string> = new Set(),
): NavItem[] {
  return NAV.flatMap((s) => {
    if (off.has(s.key)) return [];
    if (s.key === 'now') return [s];
    if (user == null) return [];
    if (s.permission === undefined || userCan(user, s.permission)) return [s];
    return s.without && on.has(s.without.module) ? [{ ...s, to: s.without.to }] : [];
  });
}

/** Where the app opens for this nav: Now, or the first section that is on. */
export function homeOf(nav: readonly NavItem[]): string {
  return nav[0]?.to ?? '/account';
}

/** Where a visitor of `/` lands. */
export type RootTarget = 'loading' | 'signin' | 'dashboard';

/** The app's landing route. */
export const DASHBOARD_HOME = '/now';

export function rootTarget(me: { isPending: boolean; data: unknown }): RootTarget {
  if (me.isPending) return 'loading';
  // A failed /me (network, 5xx) leaves data undefined: show the form rather
  // than a blank page, signing in again is the way forward either way.
  return me.data ? 'dashboard' : 'signin';
}

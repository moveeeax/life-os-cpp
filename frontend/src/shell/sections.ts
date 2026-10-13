import { Permission, userCan, type PermissionUser } from '@/lib/auth/permissions';

/**
 * The glass pill nav: the one list of the signed-in app's sections. A
 * section whose backend module is off is left out (Now too); Body and
 * Training need fitness:read. Routes keep their paths (Body is /health,
 * Training is /workout).
 */
export interface NavItem {
  key: string;
  label: string;
  to: string;
  /** Permission bit the section needs; the item is hidden without it. */
  permission?: number;
}

export const NAV: readonly NavItem[] = [
  { key: 'now', label: 'Now', to: '/now' },
  { key: 'tasks', label: 'Tasks', to: '/tasks' },
  { key: 'goals', label: 'Goals', to: '/goals' },
  { key: 'travel', label: 'Travel', to: '/travel' },
  { key: 'money', label: 'Money', to: '/money' },
  { key: 'food', label: 'Food', to: '/food' },
  { key: 'health', label: 'Body', to: '/health', permission: Permission.FitnessRead },
  { key: 'workout', label: 'Training', to: '/workout', permission: Permission.FitnessRead },
];

/** The items this user sees; `off` holds the keys of modules switched off. */
export function glassNav(
  user: PermissionUser | null | undefined,
  off: ReadonlySet<string> = new Set(),
): NavItem[] {
  return NAV.filter((s) => {
    if (off.has(s.key)) return false;
    if (s.key === 'now') return true;
    if (user == null) return false;
    return s.permission === undefined || userCan(user, s.permission);
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

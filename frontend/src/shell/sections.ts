import {
  Briefcase,
  Dumbbell,
  GitBranch,
  GraduationCap,
  Handshake,
  HeartPulse,
  Lightbulb,
  ListTodo,
  NotebookPen,
  Plane,
  Sun,
  Target,
  Wallet,
  type LucideIcon,
} from 'lucide-react';

import { Permission, userCan, type PermissionUser } from '@/lib/auth/permissions';

/**
 * Sidebar sections of the dashboard — the one list the sidebar renders.
 * A section without `path` is not built yet: it shows as an inactive item
 * with a "soon" badge. Adding a section is one entry here plus its route.
 */
export interface Section {
  key: string;
  label: string;
  icon: LucideIcon;
  /** Route of the section; absent while the section is not built. */
  path?: string;
  /** Permission bit the section needs; the item is hidden without it. */
  permission?: number;
}

export const sections: Section[] = [
  {
    key: 'health',
    label: 'Health',
    icon: HeartPulse,
    path: '/health',
    permission: Permission.FitnessRead,
  },
  { key: 'day', label: 'Day', icon: Sun },
  { key: 'money', label: 'Money', icon: Wallet },
  { key: 'tasks', label: 'Tasks', icon: ListTodo },
  { key: 'goals', label: 'Goals', icon: Target },
  { key: 'journal', label: 'Journal', icon: NotebookPen },
  { key: 'learning', label: 'Learning', icon: GraduationCap },
  {
    key: 'workout',
    label: 'Workout',
    icon: Dumbbell,
    path: '/workout',
    permission: Permission.FitnessRead,
  },
  { key: 'travel', label: 'Travel', icon: Plane },
  { key: 'work', label: 'Work', icon: Briefcase },
  { key: 'freelance', label: 'Freelance', icon: Handshake },
  { key: 'ideas', label: 'Ideas', icon: Lightbulb },
  { key: 'oss', label: 'OSS', icon: GitBranch },
];

/**
 * Sections this user sees: built ones they may open, plus every inactive one.
 * A section whose key is in `off` (its backend module is switched off) is
 * shown as not built yet.
 */
export function visibleSections(
  user: PermissionUser | null | undefined,
  off: ReadonlySet<string> = new Set(),
): Section[] {
  return sections
    .map((s) => (off.has(s.key) ? { ...s, path: undefined, permission: undefined } : s))
    .filter((s) => !s.path || s.permission === undefined || userCan(user, s.permission));
}

/** Where a visitor of `/` lands. */
export type RootTarget = 'loading' | 'signin' | 'dashboard';

/** The dashboard's landing route. */
export const DASHBOARD_HOME = '/health';

export function rootTarget(me: { isPending: boolean; data: unknown }): RootTarget {
  if (me.isPending) return 'loading';
  // A failed /me (network, 5xx) leaves data undefined: show the form rather
  // than a blank page, signing in again is the way forward either way.
  return me.data ? 'dashboard' : 'signin';
}

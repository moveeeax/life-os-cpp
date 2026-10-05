import type { ReactNode } from 'react';
import { ShieldAlert } from 'lucide-react';
import { NavLink } from 'react-router';

import { useMe } from '@/hooks/useMe';
import { ApiClientError, apiErrorMessage } from '@/lib/api/client';
import { Permission, userCan } from '@/lib/auth/permissions';
import { cn } from '@/lib/utils';

import { cardClass, secondaryButton } from './styles';

const TABS = [
  { to: '/workout', label: 'Overview', end: true },
  { to: '/workout/routines', label: 'Routines', end: false },
  { to: '/workout/exercises', label: 'Exercises', end: false },
  { to: '/workout/history', label: 'History', end: false },
];

interface WorkoutFrameProps {
  title: string;
  /** Buttons on the right of the title. */
  actions?: ReactNode;
  children: ReactNode;
}

/**
 * Frame of every Workout page: the permission check, the title row and the
 * links between the section's pages. The permission is checked here, not by
 * a route guard, for the same reason as on the Health page.
 */
export function WorkoutFrame({ title, actions, children }: WorkoutFrameProps) {
  const user = useMe().data ?? null;

  if (!userCan(user, Permission.FitnessRead)) {
    return (
      <div className={cardClass} role="alert">
        <div className="flex items-start gap-3">
          <ShieldAlert className="size-6 shrink-0 text-error-500" aria-hidden="true" />
          <div>
            <h1 className="text-theme-xl font-semibold text-gray-800 dark:text-white/90">
              No access to Workout
            </h1>
            <p className="mt-1 text-sm text-gray-500 dark:text-gray-400">
              Your role does not include reading fitness data. Ask an administrator for the Fitness
              Reader role.
            </p>
          </div>
        </div>
      </div>
    );
  }

  return (
    <div className="flex flex-col gap-4 md:gap-6">
      <div className="flex flex-wrap items-center justify-between gap-3">
        <h1 className="text-title-sm font-semibold text-gray-800 dark:text-white/90">{title}</h1>
        {actions && <div className="flex flex-wrap items-center gap-3">{actions}</div>}
      </div>
      <nav aria-label="Workout pages" className="flex gap-1 overflow-x-auto">
        {TABS.map((tab) => (
          <NavLink
            key={tab.to}
            to={tab.to}
            end={tab.end}
            className={({ isActive }) =>
              cn(
                'rounded-lg px-3 py-2 text-theme-sm font-medium whitespace-nowrap transition sm:px-4',
                isActive
                  ? 'bg-brand-50 text-brand-500 dark:bg-brand-500/12 dark:text-brand-400'
                  : 'text-gray-500 hover:bg-gray-100 dark:text-gray-400 dark:hover:bg-white/5',
              )
            }
          >
            {tab.label}
          </NavLink>
        ))}
      </nav>
      {children}
    </div>
  );
}

/** A failed query: the server's message, or a plain one when the module is off. */
export function LoadError({ error, onRetry }: { error: unknown; onRetry: () => void }) {
  const off = error instanceof ApiClientError && error.status === 404 && error.code === 'not_found';
  return (
    <div role="alert" className={cn(cardClass, 'flex flex-col items-center gap-3 text-center')}>
      <p className="text-theme-sm text-error-500">
        {off
          ? 'Not found. The workout module may be off on this server.'
          : apiErrorMessage(error, 'Failed to load.')}
      </p>
      <button type="button" onClick={onRetry} className={secondaryButton}>
        Retry
      </button>
    </div>
  );
}

/** Small text under a field with an error. */
export function FieldError({ id, message }: { id: string; message?: string }) {
  if (!message) return null;
  return (
    <p id={id} className="mt-1 text-theme-xs text-error-500">
      {message}
    </p>
  );
}

import type { ReactNode } from 'react';
import { NavLink } from 'react-router';

import { ApiClientError, apiErrorMessage } from '@/lib/api/client';
import { cn } from '@/lib/utils';

import { cardClass, secondaryButton } from '../workout/styles';

const TABS = [
  { to: '/money', label: 'Ledger', end: true },
  { to: '/money/accounts', label: 'Accounts', end: false },
  { to: '/money/categories', label: 'Categories', end: false },
  { to: '/money/reports', label: 'Reports', end: false },
  { to: '/money/advisor', label: 'Advisor', end: false },
];

interface MoneyFrameProps {
  title: string;
  /** Buttons on the right of the title. */
  actions?: ReactNode;
  children: ReactNode;
}

/**
 * Frame of every Money page: the title row and the links between the
 * section's pages. Every confirmed user keeps their own money, so there is
 * no permission check; a switched-off module answers 404 and LoadError says so.
 */
export function MoneyFrame({ title, actions, children }: MoneyFrameProps) {
  return (
    <div className="flex flex-col gap-4 md:gap-6">
      <div className="flex flex-wrap items-center justify-between gap-3">
        <h1 className="text-title-sm font-semibold text-gray-800 dark:text-white/90">{title}</h1>
        {actions && <div className="flex flex-wrap items-center gap-3">{actions}</div>}
      </div>
      <nav aria-label="Money pages" className="flex gap-1 overflow-x-auto">
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
          ? 'Not found. The money module may be off on this server.'
          : apiErrorMessage(error, 'Failed to load.')}
      </p>
      <button type="button" onClick={onRetry} className={secondaryButton}>
        Retry
      </button>
    </div>
  );
}

/** A grey block while a query loads. */
export function Placeholder({ className }: { className?: string }) {
  return (
    <div
      aria-hidden="true"
      className={cn('animate-pulse rounded-2xl bg-gray-100 dark:bg-white/5', className)}
    />
  );
}

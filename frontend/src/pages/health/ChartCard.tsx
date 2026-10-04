import type { ReactNode } from 'react';

import { apiErrorMessage } from '@/lib/api/client';

export const cardClass =
  'rounded-2xl border border-gray-200 bg-white p-5 sm:p-6 dark:border-gray-800 dark:bg-white/3';

interface ChartCardProps {
  title: string;
  /** One line under the title, e.g. what the series are. */
  hint?: string;
  isPending: boolean;
  error: unknown;
  onRetry: () => void;
  /** True when the query succeeded but there is nothing to draw. */
  isEmpty: boolean;
  emptyText?: string;
  children: ReactNode;
}

/**
 * Frame of one Health block. Each block loads on its own: a failed query
 * shows its error and a retry button here and leaves the other blocks alone.
 */
export function ChartCard({
  title,
  hint,
  isPending,
  error,
  onRetry,
  isEmpty,
  emptyText = 'No data in this period.',
  children,
}: ChartCardProps) {
  return (
    <section className={cardClass} aria-label={title}>
      <h2 className="text-lg font-semibold text-gray-800 dark:text-white/90">{title}</h2>
      {hint && <p className="mt-1 text-theme-sm text-gray-500 dark:text-gray-400">{hint}</p>}
      <div className="mt-4">
        {isPending ? (
          <div
            className="h-64 animate-pulse rounded-xl bg-gray-100 dark:bg-white/5"
            aria-label="Loading"
          />
        ) : error ? (
          <div role="alert" className="flex h-64 flex-col items-center justify-center gap-3">
            <p className="text-theme-sm text-error-500">
              {apiErrorMessage(error, 'Failed to load.')}
            </p>
            <button
              type="button"
              onClick={onRetry}
              className="rounded-lg border border-gray-300 px-4 py-2 text-theme-sm font-medium text-gray-700 hover:bg-gray-100 dark:border-gray-700 dark:text-gray-300 dark:hover:bg-white/5"
            >
              Retry
            </button>
          </div>
        ) : isEmpty ? (
          <p className="flex h-64 items-center justify-center text-theme-sm text-gray-500 dark:text-gray-400">
            {emptyText}
          </p>
        ) : (
          children
        )}
      </div>
    </section>
  );
}

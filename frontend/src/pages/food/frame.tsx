import type { ReactNode } from 'react';

import { ApiClientError, apiErrorMessage } from '@/lib/api/client';
import { cn } from '@/lib/utils';

import { SectionFrame, type TabItem } from '@/shell/kit';

const TABS: TabItem[] = [
  { to: '/food', label: 'Day', end: true },
  { to: '/food/week', label: 'Week' },
  { to: '/food/items', label: 'Products' },
  { to: '/food/goals', label: 'Goals' },
];

interface FoodFrameProps {
  /** The page's own name; the headline when the page gives none. */
  title: string;
  headline?: ReactNode;
  sub?: ReactNode;
  /** The tools line: date, the main action. */
  actions?: ReactNode;
  children: ReactNode;
}

/**
 * Frame of every Food page on the calm sheet. Any confirmed user may keep a
 * diary, so there is no permission check here; a switched-off module answers
 * 404 and LoadError says so.
 */
export function FoodFrame({ title, headline, sub, actions, children }: FoodFrameProps) {
  return (
    <SectionFrame eyebrow="Food" headline={headline ?? title} sub={sub} tabs={TABS} tools={actions}>
      {children}
    </SectionFrame>
  );
}

/** A failed query: the server's message, or a plain one when the module is off. */
export function LoadError({ error, onRetry }: { error: unknown; onRetry: () => void }) {
  const off = error instanceof ApiClientError && error.status === 404 && error.code === 'not_found';
  return (
    <div role="alert" className="k-banner">
      <span className="k-error-text">
        {off
          ? 'Not found. The food module may be off on this server.'
          : apiErrorMessage(error, 'Failed to load.')}
      </span>
      <button type="button" onClick={onRetry}>
        Retry →
      </button>
    </div>
  );
}

/** A grey block while a query loads. */
export function Placeholder({ className }: { className?: string }) {
  return <div aria-hidden="true" className={cn('k-placeholder', className)} />;
}

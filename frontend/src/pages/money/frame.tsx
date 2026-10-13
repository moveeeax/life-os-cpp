import type { ReactNode } from 'react';

import { useMoneySummary } from '@/hooks/useMoney';
import { ApiClientError, apiErrorMessage } from '@/lib/api/client';
import { cardCurrency, formatTotal, rateDay } from '@/lib/networth';
import { cn } from '@/lib/utils';
import { Num, SectionFrame, type TabItem } from '@/shell/kit';

const TABS: TabItem[] = [
  { to: '/money', label: 'Ledger', end: true },
  { to: '/money/summary', label: 'Summary' },
  { to: '/money/reports', label: 'Reports' },
  { to: '/money/accounts', label: 'Accounts' },
  { to: '/money/categories', label: 'Categories' },
  { to: '/money/currencies', label: 'Currencies' },
  { to: '/money/advisor', label: 'Advisor' },
];

function currency(): string {
  try {
    return globalThis.localStorage?.getItem('money:summary:currency') || 'USD';
  } catch {
    return 'USD';
  }
}

interface MoneyFrameProps {
  /** The page's own name, read by screen readers under the section headline. */
  title: string;
  /** The tools line: period, filter, the main action. */
  actions?: ReactNode;
  children: ReactNode;
}

/**
 * Frame of every Money page on the calm sheet: all accounts in the remembered
 * currency as the headline, the rate day under it, the tabs, the tools line.
 * Every confirmed user keeps their own money, so there is no permission check;
 * a switched-off module answers 404 and LoadError says so.
 */
export function MoneyFrame({ title, actions, children }: MoneyFrameProps) {
  const remembered = currency();
  const first = useMoneySummary(remembered, 30);
  const shown = cardCurrency(remembered, first.isError);
  // The remembered currency lost its rate: the headline shows USD instead of going away.
  const fallback = useMoneySummary(shown, 30, shown !== remembered);
  const s = shown === remembered ? first.data : fallback.data;
  const has = !!s && (s.accounts.length > 0 || s.not_converted.length > 0);
  return (
    <SectionFrame
      eyebrow="Money"
      headline={
        has ? (
          <>
            All accounts <Num>{formatTotal(s.total)}</Num> {s.currency}
          </>
        ) : (
          'Money'
        )
      }
      sub={
        has ? (
          <>
            {s.partial ? 'partial · ' : ''}rates of {rateDay(s.rate_date, s.date)} ·{' '}
            <Num>{(s.change.total >= 0 ? '+' : '−') + formatTotal(Math.abs(s.change.total))}</Num>{' '}
            in 30 days{s.change.partial ? ' (partial)' : ''}
            <span className="sr-only"> · {title}</span>
          </>
        ) : (
          <span className="sr-only">{title}</span>
        )
      }
      tabs={TABS}
      tools={actions}
    >
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
          ? 'Not found. The money module may be off on this server.'
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

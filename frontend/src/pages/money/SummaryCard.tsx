import { Link } from 'react-router';

import { useMoneySummary } from '@/hooks/useMoney';
import { cardCurrency, formatTotal, rateDay, signed } from '@/lib/networth';
import { cn } from '@/lib/utils';

import { cardClass } from '../workout/styles';

function currency(): string {
  try {
    return globalThis.localStorage?.getItem('money:summary:currency') || 'USD';
  } catch {
    return 'USD';
  }
}

/** All accounts in one currency and the 30-day change, on top of the Ledger; links to Summary. */
export function SummaryCard() {
  const remembered = currency();
  const first = useMoneySummary(remembered, 30);
  const shown = cardCurrency(remembered, first.isError);
  // The remembered currency lost its rate: the card shows USD instead of going away.
  const fallback = useMoneySummary(shown, 30, shown !== remembered);
  const d = shown === remembered ? first.data : fallback.data;
  if (!d || (d.accounts.length === 0 && d.not_converted.length === 0)) return null;
  return (
    <Link
      to="/money/summary"
      className={cn(
        cardClass,
        'flex flex-wrap items-baseline justify-between gap-3 transition hover:opacity-90',
      )}
    >
      <span className="text-theme-xs font-medium tracking-widest text-gray-500 uppercase">
        All accounts
      </span>
      <span className="flex flex-wrap items-baseline gap-x-3">
        <span className="num text-2xl font-light tabular-nums text-gray-800 dark:text-white">
          {formatTotal(d.total)} {d.currency}
        </span>
        <span className="text-theme-xs text-gray-500">
          {d.partial ? 'partial · ' : ''}rates of {rateDay(d.rate_date, d.date)}
        </span>
        <span
          className={cn(
            'num text-theme-sm tabular-nums',
            d.change.total >= 0 ? 'text-success-600 dark:text-success-500' : 'text-error-500',
          )}
        >
          {signed(d.change.total)} in 30 days{d.change.partial ? ' (partial)' : ''}
        </span>
        <span className="text-theme-sm text-gray-500">Summary →</span>
      </span>
    </Link>
  );
}

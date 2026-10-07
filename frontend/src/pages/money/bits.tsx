import { ChevronLeft, ChevronRight } from 'lucide-react';

import {
  decimalsOf,
  formatMoney,
  periodRange,
  shiftPeriod,
  signedAmount,
  type CurrencyTotal,
  type Currency,
  type PeriodKind,
  type Transaction,
} from '@/lib/ledger';
import { cn } from '@/lib/utils';

import { iconButton, inputClass } from '../workout/styles';

/** An amount in its currency, signed and colored by direction. */
export function Amount({
  row,
  currencies,
  className,
}: {
  row: Pick<Transaction, 'type' | 'amount' | 'currency'>;
  currencies?: Currency[];
  className?: string;
}) {
  const v = signedAmount(row);
  return (
    <span
      className={cn(
        'tabular-nums whitespace-nowrap',
        v < 0 ? 'text-gray-800 dark:text-white/90' : 'text-success-600 dark:text-success-500',
        className,
      )}
    >
      {v < 0 ? '−' : '+'}
      {formatMoney(Math.abs(v), row.currency, decimalsOf(currencies, row.currency))}
    </span>
  );
}

/** Income and expense per currency, side by side; never one sum. */
export function CurrencyTotals({
  totals,
  currencies,
}: {
  totals: CurrencyTotal[];
  currencies?: Currency[];
}) {
  if (totals.length === 0) return null;
  return (
    <div className="flex flex-wrap gap-x-5 gap-y-1 text-theme-xs text-gray-500 dark:text-gray-400">
      {totals.map((t) => (
        <span key={t.currency} className="tabular-nums whitespace-nowrap">
          <span className="font-medium text-gray-700 dark:text-gray-300">{t.currency}</span>{' '}
          {t.expense > 0 && (
            <>−{formatMoney(t.expense, t.currency, decimalsOf(currencies, t.currency))}</>
          )}
          {t.expense > 0 && t.income > 0 && ' · '}
          {t.income > 0 && (
            <span className="text-success-600 dark:text-success-500">
              +{formatMoney(t.income, t.currency, decimalsOf(currencies, t.currency))}
            </span>
          )}
        </span>
      ))}
    </div>
  );
}

const label = (kind: PeriodKind, date: string): string => {
  const { from, to } = periodRange(kind, date);
  const d = (s: string, o: Intl.DateTimeFormatOptions) =>
    new Date(`${s}T00:00:00`).toLocaleDateString('en-US', o);
  if (kind === 'month') return d(from, { month: 'long', year: 'numeric' });
  if (kind === 'quarter')
    return `Q${Math.floor(Number(from.slice(5, 7)) / 3) + 1} ${from.slice(0, 4)}`;
  return `${d(from, { month: 'short', day: 'numeric' })} – ${d(to, { month: 'short', day: 'numeric' })}`;
};

/** Week / month / quarter switch with previous and next. */
export function PeriodRow({
  kind,
  date,
  onChange,
}: {
  kind: PeriodKind;
  date: string;
  onChange: (kind: PeriodKind, date: string) => void;
}) {
  return (
    <div className="flex flex-wrap items-center gap-2">
      <select
        value={kind}
        onChange={(e) => onChange(e.target.value as PeriodKind, date)}
        aria-label="Period"
        className={cn(inputClass, 'h-10 w-auto')}
      >
        <option value="week">Week</option>
        <option value="month">Month</option>
        <option value="quarter">Quarter</option>
      </select>
      <button
        type="button"
        onClick={() => onChange(kind, shiftPeriod(kind, date, -1))}
        className={iconButton}
        aria-label="Previous period"
      >
        <ChevronLeft className="size-5" aria-hidden="true" />
      </button>
      <p className="min-w-32 text-center text-theme-sm font-medium text-gray-800 dark:text-white/90">
        {label(kind, date)}
      </p>
      <button
        type="button"
        onClick={() => onChange(kind, shiftPeriod(kind, date, 1))}
        className={iconButton}
        aria-label="Next period"
      >
        <ChevronRight className="size-5" aria-hidden="true" />
      </button>
    </div>
  );
}

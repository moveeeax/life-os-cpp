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
    <span className={cn('num k-amount', v > 0 && 'k-pos', className)}>
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
    <span className="k-totals">
      {totals.map((t) => (
        <span key={t.currency} className="num k-amount">
          {t.expense > 0 && (
            <>−{formatMoney(t.expense, t.currency, decimalsOf(currencies, t.currency))}</>
          )}
          {t.expense > 0 && t.income > 0 && ' · '}
          {t.income > 0 && (
            <span className="k-pos">
              +{formatMoney(t.income, t.currency, decimalsOf(currencies, t.currency))}
            </span>
          )}
        </span>
      ))}
    </span>
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
    <div className="k-stepper">
      <select
        value={kind}
        onChange={(e) => onChange(e.target.value as PeriodKind, date)}
        aria-label="Period"
        className="k-input k-compact"
      >
        <option value="week">Week</option>
        <option value="month">Month</option>
        <option value="quarter">Quarter</option>
      </select>
      <button
        type="button"
        onClick={() => onChange(kind, shiftPeriod(kind, date, -1))}
        aria-label="Previous period"
      >
        <ChevronLeft className="size-4" aria-hidden="true" />
      </button>
      <span>{label(kind, date)}</span>
      <button
        type="button"
        onClick={() => onChange(kind, shiftPeriod(kind, date, 1))}
        aria-label="Next period"
      >
        <ChevronRight className="size-4" aria-hidden="true" />
      </button>
    </div>
  );
}

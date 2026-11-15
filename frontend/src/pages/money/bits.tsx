import { ChevronLeft, ChevronRight } from 'lucide-react';

import {
  decimalsOf,
  formatMoney,
  periodRange,
  shiftPeriod,
  signedAmount,
  type Account,
  type Category,
  type CurrencyTotal,
  type Currency,
  type PeriodKind,
  type Transaction,
} from '@/lib/ledger';
import { cn } from '@/lib/utils';
import { inputClass, labelClass } from '@/shell/kit/styles';

/** A labelled account picker: name and currency per option. */
export function AccountSelect({
  id,
  value,
  onChange,
  accounts,
  label = 'Account',
}: {
  id: string;
  value: string;
  onChange: (v: string) => void;
  accounts: Account[];
  label?: string;
}) {
  return (
    <div>
      <label htmlFor={id} className={labelClass}>
        {label}
      </label>
      <select
        id={id}
        value={value}
        onChange={(e) => onChange(e.target.value)}
        className={inputClass}
      >
        <option value="">Pick the account</option>
        {accounts.map((a) => (
          <option key={a.id} value={a.id}>
            {a.name} · {a.currency}
          </option>
        ))}
      </select>
    </div>
  );
}

/**
 * A labelled category picker over the categories of one kind. `placeholder`
 * is the empty first option; null leaves it out (a saved row always has one).
 */
export function CategorySelect({
  id,
  value,
  onChange,
  categories,
  kind,
  placeholder = 'Pick a category',
}: {
  id: string;
  value: string;
  onChange: (v: string) => void;
  categories: Category[];
  kind: 'income' | 'expense';
  placeholder?: string | null;
}) {
  return (
    <div>
      <label htmlFor={id} className={labelClass}>
        Category
      </label>
      <select
        id={id}
        value={value}
        onChange={(e) => onChange(e.target.value)}
        className={inputClass}
      >
        {placeholder !== null && <option value="">{placeholder}</option>}
        {categories
          .filter((c) => c.kind === kind)
          .map((c) => (
            <option key={c.id} value={c.id}>
              {c.name}
            </option>
          ))}
      </select>
    </div>
  );
}

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

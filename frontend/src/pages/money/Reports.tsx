import { useSearchParams } from 'react-router';

import {
  useCategories,
  useCurrencies,
  useMoneySettings,
  useReport,
  useSaveSettings,
} from '@/hooks/useMoney';
import {
  customRangeFromSearch,
  formatMoney,
  periodFromSearch,
  periodRange,
  todayLocal,
  type PeriodKind,
  type Report,
} from '@/lib/ledger';
import { decimalsOf } from '@/lib/ledger';
import { cn } from '@/lib/utils';

import { cardClass, inputClass } from '../workout/styles';
import { PeriodRow } from './bits';
import { LoadError, MoneyFrame, Placeholder } from './frame';

type Block = Report['blocks'][number];

function pct(now: number, before: number | null | undefined): string | null {
  if (before === null || before === undefined || before === 0) return null;
  const d = Math.round(((now - before) / before) * 100);
  return `${d > 0 ? '+' : ''}${d}%`;
}

function CurrencyCard({
  block,
  fmt,
  categoryName,
}: {
  block: Block;
  fmt: (n: number) => string;
  categoryName: (id: string) => string;
}) {
  const vsPrev = pct(block.expense, block.prev_expense);
  const vsMedian = pct(block.expense, block.median3_expense);
  return (
    <section className={cardClass} aria-label={block.currency}>
      <div className="flex items-baseline justify-between gap-3">
        <h2 className="text-lg font-semibold text-gray-800 dark:text-white/90">{block.currency}</h2>
        <p className="text-theme-xs text-gray-500">
          fixed {Math.round(block.fixed_share * 100)}% · {fmt(block.avg_daily)} a day
        </p>
      </div>
      <dl className="mt-3 grid grid-cols-3 gap-3 text-theme-sm">
        <div>
          <dt className="text-theme-xs text-gray-500">Spent</dt>
          <dd className="font-semibold tabular-nums">{fmt(block.expense)}</dd>
        </div>
        <div>
          <dt className="text-theme-xs text-gray-500">Income</dt>
          <dd className="font-semibold tabular-nums text-success-600 dark:text-success-500">
            {fmt(block.income)}
          </dd>
        </div>
        <div>
          <dt className="text-theme-xs text-gray-500">Net</dt>
          <dd className="font-semibold tabular-nums">{fmt(block.net)}</dd>
        </div>
      </dl>
      <p className="mt-2 text-theme-xs text-gray-500">
        Projected {fmt(block.projection)}
        {vsPrev && ` · ${vsPrev} vs the previous period`}
        {vsMedian && ` · ${vsMedian} vs the median of three`}
      </p>
      {block.categories.length > 0 && (
        <table className="mt-3 w-full text-theme-sm">
          <tbody className="divide-y divide-gray-100 dark:divide-white/5">
            {block.categories.map((c) => (
              <tr key={c.category_id}>
                <td className="py-1.5">{categoryName(c.category_id)}</td>
                <td className="py-1.5 text-end tabular-nums">{fmt(c.spent)}</td>
                <td className="w-28 py-1.5 text-end text-theme-xs tabular-nums text-gray-500">
                  {c.budget != null && c.budget_share != null ? (
                    <span className={cn(c.budget_share >= 1 && 'text-error-500')}>
                      {Math.round(c.budget_share * 100)}% of {fmt(c.budget)}
                    </span>
                  ) : (
                    ''
                  )}
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      )}
    </section>
  );
}

/** A period per currency; the optional "as if" block converts at stored daily rates. */
export function MoneyReportsPage() {
  const [params, setParams] = useSearchParams();
  const { kind, date } = periodFromSearch(params, todayLocal());
  const custom = customRangeFromSearch(params, todayLocal());
  const currencies = useCurrencies().data;
  const categories = useCategories(true).data ?? [];
  const settings = useMoneySettings().data;
  const saveSettings = useSaveSettings();
  const asIf = settings?.view_currency ?? '';
  const report = useReport(custom ? 'custom' : kind, date, asIf || undefined, custom ?? undefined);
  const fmtIn = (currency: string) => (n: number) =>
    formatMoney(n, currency, decimalsOf(currencies, currency));
  const categoryName = (id: string) => categories.find((c) => c.id === id)?.name ?? 'Unknown';

  const setPeriod = (k: PeriodKind, d: string) => {
    const p = new URLSearchParams(params);
    p.set('kind', k);
    p.set('date', d);
    p.delete('from');
    p.delete('to');
    setParams(p, { replace: true });
  };
  const setCustom = (from: string, to: string) => {
    const p = new URLSearchParams(params);
    p.set('kind', 'custom');
    p.set('from', from);
    p.set('to', to);
    setParams(p, { replace: true });
  };
  const setAsIf = (code: string) => {
    if (!settings) return;
    saveSettings.mutate({
      view_currency: code || null,
      advisor_enabled: settings.advisor_enabled,
      advisor_weekday: settings.advisor_weekday,
      advisor_currencies: settings.advisor_currencies,
      advisor_note: settings.advisor_note,
    });
  };

  const r = report.data;
  return (
    <MoneyFrame
      title="Reports"
      actions={
        <select
          value={asIf}
          onChange={(e) => setAsIf(e.target.value)}
          aria-label="As if in one currency"
          className={cn(inputClass, 'h-10 w-auto')}
        >
          <option value="">Per currency only</option>
          {(currencies ?? []).map((c) => (
            <option key={c.code} value={c.code}>
              As if in {c.code}
            </option>
          ))}
        </select>
      }
    >
      <div className="flex flex-wrap items-center gap-3">
        <div role="radiogroup" aria-label="Period type" className="flex gap-1">
          {(['calendar', 'custom'] as const).map((m) => {
            const on = (m === 'custom') === (custom !== null);
            return (
              <button
                key={m}
                type="button"
                role="radio"
                aria-checked={on}
                onClick={() => {
                  if (m === 'custom') {
                    const r = periodRange(kind, date);
                    setCustom(r.from, r.to);
                  } else {
                    setPeriod(kind, custom?.to ?? date);
                  }
                }}
                className={cn(
                  'rounded-lg px-3 py-2 text-theme-sm font-medium transition',
                  on
                    ? 'bg-brand-50 text-brand-500 dark:bg-brand-500/12 dark:text-brand-400'
                    : 'text-gray-500 hover:bg-gray-100 dark:text-gray-400 dark:hover:bg-white/5',
                )}
              >
                {m === 'calendar' ? 'Calendar' : 'Custom'}
              </button>
            );
          })}
        </div>
        {custom ? (
          <div className="flex flex-wrap items-center gap-2">
            <input
              type="date"
              value={custom.from}
              max={custom.to}
              onChange={(e) => e.target.value && setCustom(e.target.value, custom.to)}
              aria-label="From"
              className={cn(inputClass, 'h-10 w-auto')}
            />
            <span className="text-theme-sm text-gray-500">to</span>
            <input
              type="date"
              value={custom.to}
              min={custom.from}
              onChange={(e) => e.target.value && setCustom(custom.from, e.target.value)}
              aria-label="To"
              className={cn(inputClass, 'h-10 w-auto')}
            />
          </div>
        ) : (
          <PeriodRow kind={kind} date={date} onChange={setPeriod} />
        )}
      </div>
      {report.isPending ? (
        <Placeholder className="h-64" />
      ) : report.isError ? (
        <LoadError error={report.error} onRetry={() => void report.refetch()} />
      ) : !r || r.blocks.length === 0 ? (
        <p className={cn(cardClass, 'text-center text-theme-sm text-gray-500')}>
          Nothing in this period.
        </p>
      ) : (
        <>
          {r.as_if && (
            <section className={cardClass} aria-label="As if">
              <p className="text-theme-xs font-medium text-gray-500 uppercase">
                As if in {r.as_if.currency}, at daily rates
              </p>
              <dl className="mt-2 grid grid-cols-3 gap-3 text-theme-sm">
                <div>
                  <dt className="text-theme-xs text-gray-500">Spent</dt>
                  <dd className="font-semibold tabular-nums">
                    {fmtIn(r.as_if.currency)(r.as_if.expense)}
                  </dd>
                </div>
                <div>
                  <dt className="text-theme-xs text-gray-500">Income</dt>
                  <dd className="font-semibold tabular-nums">
                    {fmtIn(r.as_if.currency)(r.as_if.income)}
                  </dd>
                </div>
                <div>
                  <dt className="text-theme-xs text-gray-500">Net</dt>
                  <dd className="font-semibold tabular-nums">
                    {fmtIn(r.as_if.currency)(r.as_if.net)}
                  </dd>
                </div>
              </dl>
              <p className="mt-2 text-theme-xs text-gray-500">
                {Object.entries(r.as_if.rates as Record<string, { per_usd: number; date: string }>)
                  .map(
                    ([code, v]) =>
                      `${code} ${Number(v.per_usd.toPrecision(6))} per USD (${v.date})`,
                  )
                  .join(' · ')}
                {r.as_if.partial &&
                  ` · partial: ${r.as_if.unconverted > 0 ? `${r.as_if.unconverted} of the currencies have` : `${r.as_if.currency} has`} no rate on or before the period's end`}
              </p>
              <p className="mt-1 text-theme-xs text-gray-500">
                Rates: fawazahmed0/currency-api. Stored amounts do not change.
              </p>
            </section>
          )}
          <div className="grid gap-4 lg:grid-cols-2">
            {r.blocks.map((b) => (
              <CurrencyCard
                key={b.currency}
                block={b}
                fmt={fmtIn(b.currency)}
                categoryName={categoryName}
              />
            ))}
          </div>
          {r.recurring.length > 0 && (
            <section className={cardClass} aria-label="Recurring">
              <h2 className="text-base font-semibold text-gray-800 dark:text-white/90">
                Recurring charges
              </h2>
              <ul className="mt-2 divide-y divide-gray-100 text-theme-sm dark:divide-white/5">
                {r.recurring.map((x) => (
                  <li
                    key={`${x.merchant_key}-${x.currency}`}
                    className="flex justify-between gap-3 py-1.5"
                  >
                    <span>
                      {x.merchant_key}
                      <span className="ms-1.5 text-theme-xs text-gray-500">
                        {x.times}×, next about {x.next_expected}
                      </span>
                    </span>
                    <span className="tabular-nums">{fmtIn(x.currency)(x.amount)}</span>
                  </li>
                ))}
              </ul>
            </section>
          )}
          {r.new_merchants.length > 0 && (
            <section className={cardClass} aria-label="New merchants">
              <h2 className="text-base font-semibold text-gray-800 dark:text-white/90">
                First time this period
              </h2>
              <p className="mt-1 text-theme-sm text-gray-600 dark:text-gray-400">
                {r.new_merchants.join(', ')}
              </p>
            </section>
          )}
        </>
      )}
    </MoneyFrame>
  );
}

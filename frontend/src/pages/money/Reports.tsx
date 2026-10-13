import { useSearchParams } from 'react-router';

import { useErrorToast } from '@/hooks/useErrorToast';
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

import { Facts, Filters, Group, Num, Row } from '@/shell/kit';
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
    <Group label={block.currency} total={<Num>net {fmt(block.net)}</Num>}>
      <Facts
        items={[
          { label: 'Spent', value: <Num>{fmt(block.expense)}</Num> },
          { label: 'Income', value: <Num className="k-pos">{fmt(block.income)}</Num> },
          { label: 'Projected', value: <Num>{fmt(block.projection)}</Num> },
        ]}
      />
      <p className="k-sub">
        Fixed <Num>{Math.round(block.fixed_share * 100)}%</Num> · <Num>{fmt(block.avg_daily)}</Num>{' '}
        a day
        {vsPrev && (
          <>
            {' '}
            · <Num>{vsPrev}</Num> vs the previous period
          </>
        )}
        {vsMedian && (
          <>
            {' '}
            · <Num>{vsMedian}</Num> vs the median of three
          </>
        )}
      </p>
      {block.categories.map((c) => (
        <Row
          key={c.category_id}
          title={categoryName(c.category_id)}
          right={<Num>{fmt(c.spent)}</Num>}
          sub={
            c.budget != null && c.budget_share != null ? (
              <span className={c.budget_share >= 1 ? 'k-error-text' : undefined}>
                <Num>{Math.round(c.budget_share * 100)}%</Num> of <Num>{fmt(c.budget)}</Num>
              </span>
            ) : undefined
          }
        />
      ))}
    </Group>
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
  useErrorToast(saveSettings.error);
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
  const filterCount = (custom ? 1 : 0) + (asIf ? 1 : 0);
  return (
    <MoneyFrame
      title="Reports"
      actions={
        <>
          {custom ? (
            <span className="k-sub">
              {custom.from} – {custom.to}
            </span>
          ) : (
            <PeriodRow kind={kind} date={date} onChange={setPeriod} />
          )}
          <span className="k-grow" />
          <Filters
            active={filterCount}
            onClear={() => {
              if (custom) setPeriod(kind, custom.to);
              if (asIf) setAsIf('');
            }}
          >
            <div role="radiogroup" aria-label="Period type" className="k-seg">
              {(['calendar', 'custom'] as const).map((m) => {
                const on = (m === 'custom') === (custom !== null);
                return (
                  <button
                    key={m}
                    type="button"
                    role="radio"
                    aria-checked={on}
                    className={on ? 'k-seg-on' : undefined}
                    onClick={() => {
                      if (m === 'custom') {
                        const range = periodRange(kind, date);
                        setCustom(range.from, range.to);
                      } else {
                        setPeriod(kind, custom?.to ?? date);
                      }
                    }}
                  >
                    {m === 'calendar' ? 'Calendar' : 'Custom'}
                  </button>
                );
              })}
            </div>
            {custom && (
              <div className="k-fields">
                <label className="k-field">
                  <span>From</span>
                  <input
                    type="date"
                    value={custom.from}
                    max={custom.to}
                    onChange={(e) => e.target.value && setCustom(e.target.value, custom.to)}
                  />
                </label>
                <label className="k-field">
                  <span>To</span>
                  <input
                    type="date"
                    value={custom.to}
                    min={custom.from}
                    onChange={(e) => e.target.value && setCustom(custom.from, e.target.value)}
                  />
                </label>
              </div>
            )}
            <label className="k-field">
              <span>As if in one currency</span>
              <select value={asIf} onChange={(e) => setAsIf(e.target.value)}>
                <option value="">Per currency only</option>
                {(currencies ?? []).map((c) => (
                  <option key={c.code} value={c.code}>
                    As if in {c.code}
                  </option>
                ))}
              </select>
            </label>
          </Filters>
        </>
      }
    >
      {report.isPending ? (
        <Placeholder />
      ) : report.isError ? (
        <LoadError error={report.error} onRetry={() => void report.refetch()} />
      ) : !r || r.blocks.length === 0 ? (
        <p className="k-empty">Nothing in this period.</p>
      ) : (
        <>
          {r.as_if && (
            <Group label={`As if in ${r.as_if.currency}, at daily rates`}>
              <Facts
                items={[
                  { label: 'Spent', value: <Num>{fmtIn(r.as_if.currency)(r.as_if.expense)}</Num> },
                  { label: 'Income', value: <Num>{fmtIn(r.as_if.currency)(r.as_if.income)}</Num> },
                  { label: 'Net', value: <Num>{fmtIn(r.as_if.currency)(r.as_if.net)}</Num> },
                ]}
              />
              <p className="k-sub">
                <Num>
                  {Object.entries(
                    r.as_if.rates as Record<string, { per_usd: number; date: string }>,
                  )
                    .map(
                      ([code, v]) =>
                        `${code} ${Number(v.per_usd.toPrecision(6))} per USD (${v.date})`,
                    )
                    .join(' · ')}
                </Num>
                {r.as_if.partial &&
                  ` · partial: ${r.as_if.unconverted > 0 ? `${r.as_if.unconverted} of the currencies have` : `${r.as_if.currency} has`} no rate on or before the period's end`}
                . Rates: fawazahmed0/currency-api. Stored amounts do not change.
              </p>
            </Group>
          )}
          {r.blocks.map((b) => (
            <CurrencyCard
              key={b.currency}
              block={b}
              fmt={fmtIn(b.currency)}
              categoryName={categoryName}
            />
          ))}
          {r.recurring.length > 0 && (
            <Group label="Recurring charges">
              {r.recurring.map((x) => (
                <Row
                  key={`${x.merchant_key}-${x.currency}`}
                  title={x.merchant_key}
                  sub={
                    <>
                      <Num>{x.times}×</Num>, next about {x.next_expected}
                    </>
                  }
                  right={<Num>{fmtIn(x.currency)(x.amount)}</Num>}
                />
              ))}
            </Group>
          )}
          {r.new_merchants.length > 0 && (
            <Group label="First time this period">
              <p className="k-sub" data-num-ok>
                {r.new_merchants.join(', ')}
              </p>
            </Group>
          )}
        </>
      )}
    </MoneyFrame>
  );
}

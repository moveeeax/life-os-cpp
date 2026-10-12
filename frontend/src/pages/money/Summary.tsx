import { useEffect, useRef, useState } from 'react';

import { useMoneySummary } from '@/hooks/useMoney';
import {
  SUMMARY_CURRENCIES,
  SUMMARY_PERIODS,
  chartGeometry,
  formatTotal,
  rateDay,
  rateText,
  signed,
} from '@/lib/networth';
import { cn } from '@/lib/utils';

import { cardClass } from '../workout/styles';
import { LoadError, MoneyFrame, Placeholder } from './frame';

const CURRENCY_KEY = 'money:summary:currency';
const BOX = { width: 720, height: 240, left: 56, right: 12, top: 12, bottom: 28 };

/** The drawing width follows the container, so labels keep their size on a phone. */
function useWidth(ref: React.RefObject<HTMLDivElement | null>): number {
  const [width, setWidth] = useState(BOX.width);
  useEffect(() => {
    const el = ref.current;
    if (!el || typeof ResizeObserver === 'undefined') return;
    const ro = new ResizeObserver(([e]) =>
      setWidth(Math.max(280, Math.round(e.contentRect.width))),
    );
    ro.observe(el);
    return () => ro.disconnect();
  }, [ref]);
  return width;
}

function storedCurrency(): string {
  try {
    const v = globalThis.localStorage?.getItem(CURRENCY_KEY);
    return v && (SUMMARY_CURRENCIES as readonly string[]).includes(v) ? v : 'USD';
  } catch {
    return 'USD';
  }
}

function Segmented<T extends string | number>({
  label,
  options,
  value,
  onChange,
  text,
}: {
  label: string;
  options: readonly T[];
  value: T;
  onChange: (v: T) => void;
  text: (v: T) => string;
}) {
  return (
    <div
      role="group"
      aria-label={label}
      className="inline-flex gap-0.5 rounded-full border border-gray-200 p-0.5 dark:border-white/15"
    >
      {options.map((o) => (
        <button
          key={String(o)}
          type="button"
          aria-pressed={o === value}
          onClick={() => onChange(o)}
          className={cn(
            'rounded-full px-2.5 py-0.5 text-theme-xs',
            o === value
              ? 'bg-gray-800 text-white dark:bg-white/90 dark:text-gray-900'
              : 'text-gray-500 dark:text-gray-400',
          )}
        >
          {text(o)}
        </button>
      ))}
    </div>
  );
}

const shortDay = (iso: string) =>
  new Date(`${iso}T00:00:00Z`).toLocaleDateString('en-GB', {
    day: 'numeric',
    month: 'short',
    timeZone: 'UTC',
  });

function Trend({
  days,
  currency,
}: {
  days: { date: string; total: number; partial: boolean }[];
  currency: string;
}) {
  const [hover, setHover] = useState<number | null>(null);
  const wrap = useRef<HTMLDivElement>(null);
  const width = useWidth(wrap);
  const box = { ...BOX, width, height: width < 480 ? 200 : BOX.height };
  const g = chartGeometry(days, box);
  if (g.points.length === 0) return null;
  const compact = new Intl.NumberFormat('en-US', { notation: 'compact', maximumFractionDigits: 1 });
  const last = g.points.length - 1;
  const at = hover ?? last;
  const marks = [0, Math.floor(last / 2), last];
  return (
    <div className="relative" ref={wrap}>
      <svg
        viewBox={`0 0 ${box.width} ${box.height}`}
        className="block h-auto w-full text-gray-800 dark:text-white"
        role="img"
        aria-label={`All accounts in ${currency} by day`}
        onPointerLeave={() => setHover(null)}
        onPointerMove={(e) => {
          const r = e.currentTarget.getBoundingClientRect();
          const px = ((e.clientX - r.left) / r.width) * box.width;
          const share = (px - box.left) / (box.width - box.left - box.right);
          setHover(Math.max(0, Math.min(last, Math.round(share * last))));
        }}
      >
        <defs>
          <linearGradient id="nw-fill" x1="0" y1="0" x2="0" y2="1">
            <stop offset="0" stopColor="currentColor" stopOpacity="0.2" />
            <stop offset="1" stopColor="currentColor" stopOpacity="0" />
          </linearGradient>
        </defs>
        {g.ticks.map((t) => (
          <g key={t.y}>
            <line
              x1={box.left}
              x2={box.width - box.right}
              y1={t.y}
              y2={t.y}
              stroke="currentColor"
              strokeOpacity="0.08"
            />
            <text
              x={box.left - 8}
              y={t.y + 4}
              textAnchor="end"
              fontSize="11"
              fill="currentColor"
              fillOpacity="0.56"
            >
              {compact.format(t.value)}
            </text>
          </g>
        ))}
        {marks.map((i, k) => (
          <text
            key={i}
            x={g.points[i].x}
            y={box.height - 8}
            textAnchor={k === 0 ? 'start' : k === 2 ? 'end' : 'middle'}
            fontSize="11"
            fill="currentColor"
            fillOpacity="0.56"
          >
            {shortDay(days[i].date)}
          </text>
        ))}
        <path d={g.area} fill="url(#nw-fill)" />
        <path
          d={g.line}
          fill="none"
          stroke="currentColor"
          strokeWidth="2"
          strokeLinejoin="round"
          strokeLinecap="round"
        />
        {hover !== null && (
          <line
            x1={g.points[at].x}
            x2={g.points[at].x}
            y1={box.top}
            y2={box.height - box.bottom}
            stroke="currentColor"
            strokeOpacity="0.4"
            strokeDasharray="3 3"
          />
        )}
        <circle
          cx={g.points[at].x}
          cy={g.points[at].y}
          r="4.5"
          fill="currentColor"
          stroke="rgb(14 20 36)"
          strokeWidth="2"
        />
      </svg>
      {hover !== null && (
        <div
          className="pointer-events-none absolute -translate-x-1/2 -translate-y-[115%] rounded-lg border border-gray-200 bg-white px-2.5 py-1.5 text-theme-xs whitespace-nowrap shadow-theme-md dark:border-white/15 dark:bg-gray-900"
          style={{
            left: `${(g.points[at].x / box.width) * 100}%`,
            top: `${(g.points[at].y / box.height) * 100}%`,
          }}
        >
          {shortDay(days[at].date)}
          <br />
          <b className="num font-semibold tabular-nums">
            {formatTotal(days[at].total)} {currency}
          </b>
          {days[at].partial && <span className="text-gray-500"> · partial</span>}
        </div>
      )}
    </div>
  );
}

/** All accounts in one currency at the day's rates: the total, its change, the line by day, the accounts. */
export function MoneySummaryPage() {
  const [currency, setCurrencyState] = useState(storedCurrency);
  const [days, setDays] = useState<number>(90);
  const q = useMoneySummary(currency, days);
  const setCurrency = (c: string) => {
    setCurrencyState(c);
    try {
      globalThis.localStorage?.setItem(CURRENCY_KEY, c);
    } catch {
      // Remembering the choice is a convenience.
    }
  };
  const d = q.data;
  return (
    <MoneyFrame title="Money">
      {q.error ? (
        <LoadError error={q.error} onRetry={() => q.refetch()} />
      ) : !d ? (
        <Placeholder className="h-64" />
      ) : (
        <>
          <section className={cardClass} aria-labelledby="nw-total">
            <div className="flex flex-wrap items-center justify-between gap-3">
              <h2
                id="nw-total"
                className="text-theme-xs font-medium tracking-widest text-gray-500 uppercase"
              >
                All accounts
              </h2>
              <Segmented
                label="Currency"
                options={SUMMARY_CURRENCIES}
                value={currency as (typeof SUMMARY_CURRENCIES)[number]}
                onChange={setCurrency}
                text={(c) => c}
              />
            </div>
            <div className="mt-3 flex flex-wrap items-baseline gap-x-4 gap-y-1">
              <span className="num text-5xl font-light tracking-tight tabular-nums text-gray-800 dark:text-white">
                {formatTotal(d.total)} <span className="text-2xl">{d.currency}</span>
              </span>
              <span
                className={cn(
                  'num text-theme-sm tabular-nums',
                  d.change.total >= 0 ? 'text-success-600 dark:text-success-500' : 'text-error-500',
                )}
              >
                {d.change.total >= 0 ? '▲' : '▼'} {signed(d.change.total)} {d.currency}{' '}
                <span className="text-gray-500">in {days} days</span>
              </span>
            </div>
            <dl className="mt-4 grid gap-x-6 gap-y-2 text-theme-sm sm:grid-cols-2">
              <div className="flex justify-between gap-3 border-t border-gray-100 pt-2 dark:border-white/10">
                <dt className="text-gray-500">From the ledger (income − spending)</dt>
                <dd className="num font-medium tabular-nums">{signed(d.change.ledger)}</dd>
              </div>
              <div className="flex justify-between gap-3 border-t border-gray-100 pt-2 dark:border-white/10">
                <dt className="text-gray-500">From the rates</dt>
                <dd className="num font-medium tabular-nums">{signed(d.change.rates)}</dd>
              </div>
            </dl>
            <p className="mt-3 text-theme-xs text-gray-500">
              Converted at the rates of {rateDay(d.rate_date, d.date)}, daily quotes against the US
              dollar.
              {d.partial && ' Partial: some money has no rate and is listed below.'}
              {d.change.partial &&
                ` The change leaves out money without a rate on ${rateDay(d.change.from, d.date)} or ${rateDay(d.date, d.date)}.`}
            </p>
          </section>

          <section className={cardClass} aria-labelledby="nw-days">
            <div className="flex flex-wrap items-center justify-between gap-3">
              <h2
                id="nw-days"
                className="text-theme-xs font-medium tracking-widest text-gray-500 uppercase"
              >
                By day
              </h2>
              <Segmented
                label="Period"
                options={SUMMARY_PERIODS}
                value={days as (typeof SUMMARY_PERIODS)[number]}
                onChange={setDays}
                text={(p) => `${p} days`}
              />
            </div>
            <div className="mt-3">
              <Trend days={d.days} currency={d.currency} />
            </div>
            <p className="mt-2 text-theme-xs text-gray-500">
              Each day: the balances of that day at that day's rates.
            </p>
          </section>

          <section className={cardClass} aria-labelledby="nw-accounts">
            <h2
              id="nw-accounts"
              className="text-theme-xs font-medium tracking-widest text-gray-500 uppercase"
            >
              Accounts
            </h2>
            <div className="mt-3 overflow-x-auto">
              <table className="w-full min-w-[520px] text-theme-sm">
                <thead>
                  <tr className="text-theme-xs text-gray-500">
                    <th className="pb-2 text-start font-medium">Account</th>
                    <th className="pb-2 text-end font-medium">Balance</th>
                    <th className="pb-2 text-end font-medium">Rate</th>
                    <th className="pb-2 text-end font-medium">In {d.currency}</th>
                  </tr>
                </thead>
                <tbody className="divide-y divide-gray-100 dark:divide-white/10">
                  {d.accounts.map((a) => (
                    <tr key={a.id}>
                      <td className="py-2">
                        {a.name}
                        <span className="block text-theme-xs text-gray-500 capitalize">
                          {a.kind}
                        </span>
                      </td>
                      <td className="num py-2 text-end tabular-nums">
                        {formatTotal(a.balance)} {a.currency}
                      </td>
                      <td className="py-2 text-end tabular-nums">
                        <span className="num">{rateText(a.rate, a.currency, d.currency)}</span>
                        <span className="block text-theme-xs text-gray-500">
                          {rateDay(a.rate_date, d.date)}
                        </span>
                      </td>
                      <td className="num py-2 text-end tabular-nums">
                        {formatTotal(a.value)} {d.currency}
                        <span className="block text-theme-xs text-gray-500">
                          {d.total > 0 ? Math.round((a.value / d.total) * 100) : 0}%
                        </span>
                      </td>
                    </tr>
                  ))}
                  <tr className="font-semibold">
                    <td className="py-2">All accounts</td>
                    <td />
                    <td />
                    <td className="num py-2 text-end tabular-nums">
                      {formatTotal(d.total)} {d.currency}
                    </td>
                  </tr>
                </tbody>
              </table>
            </div>
            {d.not_converted.length > 0 && (
              <p className="mt-3 text-theme-xs text-warning-600 dark:text-warning-400">
                Not converted (no rate):{' '}
                {d.not_converted.map((n) => `${formatTotal(n.amount)} ${n.currency}`).join(', ')}.
                The total leaves it out.
              </p>
            )}
          </section>
        </>
      )}
    </MoneyFrame>
  );
}

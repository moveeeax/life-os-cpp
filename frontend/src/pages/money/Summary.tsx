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

import { Facts, Group, Num, Row } from '@/shell/kit';
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
    <div role="group" aria-label={label} className="k-seg">
      {options.map((o) => (
        <button
          key={String(o)}
          type="button"
          aria-pressed={o === value}
          onClick={() => onChange(o)}
          className={o === value ? 'k-seg-on' : undefined}
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
              className="num"
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
    <MoneyFrame
      title="Summary"
      actions={
        <>
          <Segmented
            label="Currency"
            options={SUMMARY_CURRENCIES}
            value={currency as (typeof SUMMARY_CURRENCIES)[number]}
            onChange={setCurrency}
            text={(c) => c}
          />
          <span className="k-grow" />
          <Segmented
            label="Period"
            options={SUMMARY_PERIODS}
            value={days as (typeof SUMMARY_PERIODS)[number]}
            onChange={setDays}
            text={(p) => `${p} days`}
          />
        </>
      }
    >
      {q.error ? (
        <LoadError error={q.error} onRetry={() => q.refetch()} />
      ) : !d ? (
        <Placeholder />
      ) : (
        <>
          <Facts
            items={[
              {
                label: `In ${days} days`,
                value: (
                  <Num className={d.change.total >= 0 ? 'k-pos' : undefined}>
                    {signed(d.change.total)} {d.currency}
                  </Num>
                ),
              },
              { label: 'From the ledger', value: <Num>{signed(d.change.ledger)}</Num> },
              { label: 'From the rates', value: <Num>{signed(d.change.rates)}</Num> },
            ]}
          />
          <Group label="By day">
            <Trend days={d.days} currency={d.currency} />
          </Group>
          <Group
            label="Accounts"
            total={
              <Num>
                {formatTotal(d.total)} {d.currency}
              </Num>
            }
          >
            {d.accounts.map((a) => (
              <Row
                key={a.id}
                title={a.name}
                sub={<span className="k-cap">{a.kind}</span>}
                right={
                  <Num>
                    {formatTotal(a.value)} {d.currency}
                  </Num>
                }
              >
                <span>
                  Balance{' '}
                  <Num>
                    {formatTotal(a.balance)} {a.currency}
                  </Num>
                </span>
                <span>
                  Rate <Num>{rateText(a.rate, a.currency, d.currency)}</Num> on{' '}
                  {rateDay(a.rate_date, d.date)}
                </span>
                <span>
                  Share <Num>{d.total > 0 ? Math.round((a.value / d.total) * 100) : 0}%</Num>
                </span>
              </Row>
            ))}
          </Group>
          {d.not_converted.length > 0 && (
            <p className="k-sub k-warn-text">
              Not converted (no rate):{' '}
              <Num>
                {d.not_converted.map((n) => `${formatTotal(n.amount)} ${n.currency}`).join(', ')}
              </Num>
              . The total leaves it out.
            </p>
          )}
          <p className="k-sub">
            Converted at the rates of {rateDay(d.rate_date, d.date)}, daily quotes against the US
            dollar. Each day of the line: that day's balances at that day's rates.
            {d.change.partial &&
              ` The change leaves out money without a rate on ${rateDay(d.change.from, d.date)} or ${rateDay(d.date, d.date)}.`}
          </p>
        </>
      )}
    </MoneyFrame>
  );
}

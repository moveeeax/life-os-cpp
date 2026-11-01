import { useEffect, useRef } from 'react';

import { useHideNumbers } from '@/hooks/useHideNumbers';
import { useMonthTotals } from '@/hooks/useMoney';
import { formatTotal } from '@/lib/networth';
import { median, monthSpent, tone, toneColor } from '@/lib/ledger/weeks';

const MONTHS = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
const NAMES = [
  'January',
  'February',
  'March',
  'April',
  'May',
  'June',
  'July',
  'August',
  'September',
  'October',
  'November',
  'December',
];

/**
 * Twelve months of the period's year, each filled by what it spent against the
 * median of the closed months ("as if" in one currency at the report rates).
 */
export function MonthStrip({
  year,
  month,
  today,
  onPick,
}: {
  year: number;
  /** The picked month, 1 to 12, or null when the period is not a month. */
  month: number | null;
  today: string;
  onPick: (date: string) => void;
}) {
  const thisYear = Number(today.slice(0, 4));
  const running = year === thisYear;
  // The last month with a report: December of a past year, this month of this one.
  const last = year < thisYear ? 12 : running ? Number(today.slice(5, 7)) : 0;
  const totals = useMonthTotals(year, last > 0).data;
  const [hidden] = useHideNumbers();
  const spent = monthSpent(totals, year, last);
  const currency = totals?.find((t) => t.as_if)?.as_if?.currency ?? '';
  // The month still going on does not set the median.
  const closed = spent.slice(0, running ? last - 1 : last).filter((v): v is number => v !== null);
  const med = median(closed);
  const wrap = useRef<HTMLDivElement>(null);
  useEffect(() => {
    const el = wrap.current;
    if (el && el.scrollWidth > el.clientWidth + 4) el.scrollLeft = el.scrollWidth - el.clientWidth;
  }, [year]);

  return (
    <div className="m-strip-block">
      <div className="m-strip-wrap" ref={wrap}>
        <div className="m-strip" role="group" aria-label={`Spending by month, ${year}`}>
          {MONTHS.map((m, i) => {
            const v = spent[i];
            const n = i + 1;
            if (n > last) {
              return (
                <button key={m} type="button" className="m-future" disabled>
                  <span className="m-mn">{m}</span>
                  <span className="m-mv">&nbsp;</span>
                </button>
              );
            }
            const fill = v !== null && med ? toneColor(tone(v, med)) : null;
            const date = `${year}-${String(n).padStart(2, '0')}-01`;
            const label =
              v === null
                ? `${NAMES[i]}, loading`
                : hidden
                  ? NAMES[i]
                  : `${NAMES[i]}, about ${formatTotal(v)} ${currency}${running && n === last ? ', so far' : ''}`;
            return (
              <button
                key={m}
                type="button"
                aria-pressed={month === n}
                aria-label={label}
                onClick={() => onPick(date)}
                style={
                  fill
                    ? { background: fill.background, color: fill.dark ? '#141832' : '#fff' }
                    : undefined
                }
              >
                <span className="m-mn">{m}</span>
                <span className="m-mv num">{v === null ? '…' : short(v)}</span>
              </button>
            );
          })}
        </div>
      </div>
      {med !== null && (
        <div className="m-legend">
          <span>less</span>
          <span className="m-grad" aria-hidden="true" />
          <span>more</span>
          <span className="num">
            median ≈ {formatTotal(med)} {currency} a month
          </span>
        </div>
      )}
    </div>
  );
}

function short(v: number): string {
  if (v >= 1e6) return `${(v / 1e6).toFixed(2)}M`;
  if (v >= 1e3) return `${Math.round(v / 1e3)}K`;
  return String(Math.round(v));
}

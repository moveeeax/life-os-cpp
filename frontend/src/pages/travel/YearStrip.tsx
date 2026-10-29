import { useEffect, useRef } from 'react';

import { tripDays, yearPct, type TravelTrip } from '@/lib/travel';

const MONTHS = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];

/** The year from January to December: a segment per trip on its dates, a line for today. */
export function YearStrip({
  year,
  trips,
  today,
  selected,
  onPick,
}: {
  year: number;
  trips: TravelTrip[];
  today: string;
  selected: string[];
  onPick: (id: string) => void;
}) {
  const y0 = Date.UTC(year, 0, 1);
  const span = Date.UTC(year + 1, 0, 1) - y0;
  const wrap = useRef<HTMLDivElement>(null);
  const last = trips.reduce((m, t) => Math.max(m, yearPct(t.end_date, year)), 0);
  // On a phone the strip is wider than the sheet: open it at the year's latest trip.
  useEffect(() => {
    const el = wrap.current;
    if (!el || el.scrollWidth <= el.clientWidth + 4) return;
    el.scrollLeft = Math.max(0, (last / 100) * el.scrollWidth - el.clientWidth + 48);
  }, [year, last]);
  return (
    <div className="t-strip-wrap" ref={wrap}>
      <div className="t-strip">
        <div className="t-bar">
          {trips.map((t) => {
            const left = yearPct(t.start_date, year);
            // A day trip still gets a segment a finger can hit.
            const width = Math.max(yearPct(t.end_date, year) - left + 100 / 365, 1.4);
            const live = tripDays(t, today).live;
            return (
              <button
                key={t.id}
                type="button"
                className={live ? 't-seg t-now' : 't-seg'}
                style={{ left: `${left}%`, width: `${width}%` }}
                aria-label={`${t.name}, ${t.start_date} to ${t.end_date}`}
                aria-pressed={selected.includes(t.id)}
                onClick={() => onPick(t.id)}
                data-num-ok
              >
                {width > 6 ? t.name.replace(/\s*\([^)]*\)/, '') : ''}
              </button>
            );
          })}
          {today.startsWith(String(year)) && (
            <span className="t-today" style={{ left: `${yearPct(today, year)}%` }} />
          )}
        </div>
        <div className="t-axis" aria-hidden="true">
          {MONTHS.map((m, i) => (
            <span key={m} style={{ left: `${((Date.UTC(year, i, 15) - y0) / span) * 100}%` }}>
              {m}
            </span>
          ))}
        </div>
      </div>
    </div>
  );
}

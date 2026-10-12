import { useEffect, useState } from 'react';
import { Link } from 'react-router';

import { useMe } from '@/hooks/useMe';
import { useToday } from '@/hooks/useNow';
import { greeting, localDate, quietLines } from '@/lib/now';

import { FocusLine } from './FocusLine';
import { FocusPicker } from './FocusPicker';
import './now.css';

/** The current time, updated on the minute. */
function useMinute(): Date {
  const [now, setNow] = useState(() => new Date());
  useEffect(() => {
    let interval: ReturnType<typeof setInterval> | undefined;
    const first = setTimeout(
      () => {
        setNow(new Date());
        interval = setInterval(() => setNow(new Date()), 60_000);
      },
      60_000 - (Date.now() % 60_000),
    );
    return () => {
      clearTimeout(first);
      if (interval) clearInterval(interval);
    };
  }, []);
  return now;
}

/**
 * Now: the picture, the clock, a greeting, the day's one focus and up to
 * three quiet lines when something is off. Paints from the last answer of
 * the day at once.
 */
export function NowPage() {
  const me = useMe().data ?? null;
  const tz = Intl.DateTimeFormat().resolvedOptions().timeZone || 'UTC';
  const now = useMinute();
  const date = localDate(now, tz);
  const hour =
    Number(
      new Intl.DateTimeFormat('en-GB', { timeZone: tz, hour: '2-digit', hour12: false }).format(
        now,
      ),
    ) % 24;
  const today = useToday(me?.id ?? null, date, tz);
  const [picking, setPicking] = useState(false);
  const data = today.data;
  const focus = data?.focus ?? null;
  const clock = new Intl.DateTimeFormat('en-GB', {
    timeZone: tz,
    hour: '2-digit',
    minute: '2-digit',
  }).format(now);

  return (
    <div className="n-now">
      <div className="n-where">{tz.replace(/_/g, ' ')}</div>
      <div className="n-center">
        <div className="n-clock" aria-label={`Time ${clock}`}>
          {clock}
        </div>
        <div className="n-hello">{greeting(hour, me?.first_name)}</div>
        <div className="n-focus">
          {focus && !picking ? (
            <>
              <span className="n-label">Today</span>
              <FocusLine focus={focus} onChange={() => setPicking(true)} />
            </>
          ) : data || !today.isPending ? (
            <FocusPicker date={date} today={data} onDone={() => setPicking(false)} />
          ) : null}
          {picking && (
            <button type="button" className="n-change" onClick={() => setPicking(false)}>
              Keep the current focus
            </button>
          )}
        </div>
        {data && (
          <nav className="n-lines" aria-label="Worth a look">
            {quietLines(data, hour).map((l) => (
              <Link key={l.key} to={l.to}>
                {l.parts.map((p, i) =>
                  p.num ? (
                    <span key={i} className="num">
                      {p.text}
                    </span>
                  ) : (
                    <span key={i}>{p.text}</span>
                  ),
                )}
              </Link>
            ))}
          </nav>
        )}
        {today.isError && (
          <span className="n-note">
            {data ? 'Offline: showing the last answer' : 'Could not load the day'}
          </span>
        )}
      </div>
    </div>
  );
}

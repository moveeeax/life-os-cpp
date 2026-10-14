import { Link } from 'react-router';

import { useErrorToast } from '@/hooks/useErrorToast';
import { useHabitDays, useTickHabit } from '@/hooks/useHabits';
import { dayOf, formatValue, type HabitDays } from '@/lib/habits';

function HabitTick({ h, date }: { h: HabitDays; date: string }) {
  const tick = useTickHabit();
  useErrorToast(tick.error);
  const d = dayOf(h, date);
  const done = d?.done ?? false;
  const manual = d?.manual ?? false;
  const fromData = done && !manual;
  const pressed = tick.isPending ? !done : done;
  const right =
    h.schedule === 'weekly' ? (
      <>
        <span className="num">{h.week_done ?? 0}</span> of{' '}
        <span className="num">{h.per_week ?? 1}</span> this week
      </>
    ) : h.no_data ? (
      'no data'
    ) : d?.value != null && h.source !== 'manual' ? (
      <>
        auto · <span className="num">{formatValue(h, d.value)}</span>
      </>
    ) : null;
  return (
    <div className={pressed ? 'c-row c-done' : 'c-row'}>
      <button
        type="button"
        className="c-ring"
        aria-pressed={pressed}
        aria-label={fromData ? `Done from data: ${h.name}` : `Done: ${h.name}`}
        title={fromData ? 'Ticked from data' : undefined}
        disabled={tick.isPending || fromData}
        onClick={() => tick.mutate({ id: h.id, date, on: !manual })}
      />
      <span className="c-t" data-num-ok>
        {h.name}
      </span>
      <span className="c-r">{right}</span>
    </div>
  );
}

/** The Habits plate opened: today's habits with their ticks, every day first. */
export function HabitsPanel({ date }: { date: string }) {
  const q = useHabitDays(date, date);
  const list = q.data?.data ?? [];
  const daily = list.filter((h) => h.schedule === 'daily');
  const weekly = list.filter((h) => h.schedule === 'weekly');
  return (
    <>
      <h2>
        Habits today <Link to="/habits">Open Habits →</Link>
      </h2>
      {q.isPending ? (
        <p className="c-empty">Loading…</p>
      ) : q.isError ? (
        <p className="c-empty">Habits did not load.</p>
      ) : list.length === 0 ? (
        <p className="c-empty">No habits yet.</p>
      ) : (
        <>
          {daily.length > 0 && (
            <div>
              <span className="c-label">Every day</span>
              {daily.map((h) => (
                <HabitTick key={h.id} h={h} date={date} />
              ))}
            </div>
          )}
          {weekly.length > 0 && (
            <div>
              <span className="c-label">This week</span>
              {weekly.map((h) => (
                <HabitTick key={h.id} h={h} date={date} />
              ))}
            </div>
          )}
        </>
      )}
    </>
  );
}

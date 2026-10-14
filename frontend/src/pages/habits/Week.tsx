import { useState } from 'react';

import { useErrorToast } from '@/hooks/useErrorToast';
import { habitsZone, useHabitDays, useTickHabit } from '@/hooks/useHabits';
import {
  addDays,
  dayCount,
  longDay,
  mondayOf,
  weekCells,
  weekDates,
  weekLine,
  type Cell,
  type HabitDays,
} from '@/lib/habits';
import { localDate } from '@/lib/now';
import { Num } from '@/shell/kit';

import { HabitsFrame, LoadError, Placeholder } from './frame';

const LETTERS = ['M', 'T', 'W', 'T', 'F', 'S', 'S'];

const CELL_WORDS: Record<Cell, string> = {
  manual: 'ticked by hand',
  auto: 'ticked from data',
  open: 'not done',
  future: 'still to come',
};

function WeekLine({ h, monday, today }: { h: HabitDays; monday: string; today: string }) {
  const tick = useTickHabit();
  useErrorToast(tick.error);
  const cells = weekCells(h, monday, today);
  const dates = weekDates(monday);
  return (
    <>
      <span className="h-n" data-num-ok>
        {h.name}
      </span>
      {cells.map((cell, i) => (
        <button
          key={dates[i]}
          type="button"
          className={`h-c h-c-${cell}`}
          aria-label={`${h.name}, ${longDay(dates[i])}: ${CELL_WORDS[cell]}`}
          title={cell === 'auto' ? 'Ticked from data' : undefined}
          disabled={cell === 'future' || cell === 'auto' || tick.isPending}
          onClick={() => tick.mutate({ id: h.id, date: dates[i], on: cell !== 'manual' })}
        />
      ))}
      <span className="h-end">
        {h.no_data ? (
          <span className="k-muted">no data</span>
        ) : h.schedule === 'weekly' ? (
          <>
            <Num>{h.week_done ?? 0}</Num> of <Num>{h.per_week ?? 1}</Num>
          </>
        ) : (
          <>
            <Num>{h.streak}</Num> {h.streak === 1 ? 'day' : 'days'}
          </>
        )}
      </span>
    </>
  );
}

/** One line of seven days per habit; a click on a past day ticks it by hand. */
export function HabitsWeekPage() {
  const today = localDate(new Date(), habitsZone());
  const thisMonday = mondayOf(today);
  const [monday, setMonday] = useState(thisMonday);
  const q = useHabitDays(monday, addDays(monday, 6));
  const list = q.data?.data ?? [];
  const day = q.data?.today ?? today;
  const current = monday === thisMonday;
  const count = dayCount(list, day);
  return (
    <HabitsFrame
      headline={
        current && list.length > 0 ? (
          <>
            <Num>{count.done}</Num> of <Num>{count.total}</Num> today
          </>
        ) : (
          <span data-num-ok>Week of {weekLine(monday)}</span>
        )
      }
      sub={
        <span data-num-ok>
          {weekLine(monday)} · {current ? 'this week' : 'past week'}
        </span>
      }
      tools={
        <>
          <button
            type="button"
            className="k-btn"
            aria-label="The week before"
            onClick={() => setMonday(addDays(monday, -7))}
          >
            ←
          </button>
          <button
            type="button"
            className="k-btn"
            aria-label="The week after"
            disabled={current}
            onClick={() => setMonday(addDays(monday, 7))}
          >
            →
          </button>
        </>
      }
    >
      {q.isPending ? (
        <Placeholder />
      ) : q.isError ? (
        <LoadError error={q.error} onRetry={() => void q.refetch()} />
      ) : list.length === 0 ? (
        <p className="k-empty">No habits yet.</p>
      ) : (
        <>
          <div className="h-week">
            <span />
            {LETTERS.map((l, i) => (
              <span key={i} className="h-h" aria-hidden="true">
                {l}
              </span>
            ))}
            <span />
            {list.map((h) => (
              <WeekLine key={h.id} h={h} monday={monday} today={day} />
            ))}
          </div>
          <p className="h-note">
            Filled: ticked by hand. Green: ticked from Mi or Training. A click on a past day ticks
            or unticks it by hand.
          </p>
        </>
      )}
    </HabitsFrame>
  );
}

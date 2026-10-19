import { useState } from 'react';

import { useErrorToast } from '@/hooks/useErrorToast';
import { habitsZone, useHabitDays, useMood, useTickHabit } from '@/hooks/useHabits';
import {
  addDays,
  canGoBack,
  dayCount,
  hasTarget,
  longDay,
  moodOf,
  moodWord,
  mondayOf,
  oldestTickDay,
  weekCells,
  weekDates,
  weekLine,
  type Cell,
  type HabitDays,
  type MoodCheckin,
} from '@/lib/habits';
import { localDate } from '@/lib/now';
import { Num } from '@/shell/kit';

import { HabitsFrame, LoadError, Placeholder } from './frame';
import { MoodSheet } from './Mood';

const LETTERS = ['M', 'T', 'W', 'T', 'F', 'S', 'S'];

const CELL_WORDS: Record<Cell, string> = {
  manual: 'ticked by hand',
  auto: 'ticked from data',
  partial: 'short of the target',
  open: 'not done',
  future: 'still to come',
};

function WeekLine({ h, monday, today }: { h: HabitDays; monday: string; today: string }) {
  const tick = useTickHabit();
  useErrorToast(tick.error);
  const cells = weekCells(h, monday, today);
  const dates = weekDates(monday);
  // The API refuses a tick more than 366 days back.
  const oldest = oldestTickDay(today);
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
          disabled={cell === 'future' || cell === 'auto' || dates[i] < oldest || tick.isPending}
          onClick={() =>
            tick.mutate({
              id: h.id,
              date: dates[i],
              on: cell !== 'manual',
              // A habit with a target is filled to it: a bare tick would not count.
              value: hasTarget(h) && h.target != null ? h.target : undefined,
            })
          }
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

/** The mood of each day of the week; a click on a day up to today checks it in. */
function MoodLine({
  monday,
  today,
  list,
}: {
  monday: string;
  today: string;
  list: MoodCheckin[] | undefined;
}) {
  const [editing, setEditing] = useState<string | null>(null);
  const dates = weekDates(monday);
  const oldest = oldestTickDay(today);
  const scores = dates.map((d) => moodOf(list, d)?.score ?? null);
  const known = scores.filter((s): s is number => s != null);
  const mean = known.length > 0 ? known.reduce((a, b) => a + b, 0) / known.length : null;
  return (
    <>
      <span className="h-n h-mood-n">Mood</span>
      {dates.map((d, i) => (
        <button
          key={d}
          type="button"
          className={`h-m${scores[i] != null ? ' h-m-on' : ''}`}
          aria-label={`Mood, ${longDay(d)}: ${scores[i] != null ? `${scores[i]}, ${moodWord(scores[i] ?? 0)}` : 'not checked in'}`}
          disabled={d > today || d < oldest}
          onClick={() => setEditing(d)}
        >
          {scores[i] != null ? <Num>{scores[i]}</Num> : null}
        </button>
      ))}
      <span className="h-end">
        {mean != null ? (
          <>
            <Num>{mean.toFixed(1)}</Num> avg
          </>
        ) : (
          <span className="k-muted">none</span>
        )}
      </span>
      {editing && (
        <MoodSheet date={editing} mood={moodOf(list, editing)} onClose={() => setEditing(null)} />
      )}
    </>
  );
}

/** One line of seven days per habit; a click on a past day ticks it by hand. */
export function HabitsWeekPage() {
  const today = localDate(new Date(), habitsZone());
  const thisMonday = mondayOf(today);
  const [monday, setMonday] = useState(thisMonday);
  const q = useHabitDays(monday, addDays(monday, 6));
  const mood = useMood(monday, addDays(monday, 6));
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
            disabled={!canGoBack(monday, day)}
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
            {!mood.isError && <MoodLine monday={monday} today={day} list={mood.data} />}
          </div>
          <p className="h-note" data-num-ok>
            Filled: ticked by hand. Green: ticked from Mi or Training. Half: short of the daily
            target. A click on a past day ticks or unticks it by hand; a habit with a target is
            filled to it. Mood shows the score of each day, 1 to 5.
          </p>
        </>
      )}
    </HabitsFrame>
  );
}

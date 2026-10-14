import { useState } from 'react';
import { ChevronLeft, ChevronRight } from 'lucide-react';
import { Link, useSearchParams } from 'react-router';

import { Modal } from '@/components/Modal';
import { useFoodDay } from '@/hooks/useFood';
import {
  MEALS,
  formatDate,
  formatGrams,
  formatKcal,
  mealByHour,
  parseDateParam,
  percent,
  shiftDate,
  todayLocal,
  type FoodDay,
  type FoodEntry,
  type FoodMeal,
  type FoodTargets,
} from '@/lib/food';

import { Group, Meter, Num } from '@/shell/kit';
import { AddEntry } from './AddEntry';
import { EntryEditor, EntryRow } from './EntryRow';
import { FoodFrame, LoadError, Placeholder } from './frame';

/** The date of the page lives in the URL; a value that is not a date means today. */
function useDayParam(): [string, (date: string) => void] {
  const [params, setParams] = useSearchParams();
  const today = todayLocal();
  const date = parseDateParam(params.get('date'), today);
  const set = (next: string) => {
    const p = new URLSearchParams(params);
    if (next === today) p.delete('date');
    else p.set('date', next);
    setParams(p, { replace: true });
  };
  return [date, set];
}

function DateRow({ date, onChange }: { date: string; onChange: (d: string) => void }) {
  const today = todayLocal();
  return (
    <div className="k-stepper">
      <button type="button" onClick={() => onChange(shiftDate(date, -1))} aria-label="Previous day">
        <ChevronLeft className="size-4" aria-hidden="true" />
      </button>
      <span>{date === today ? 'Today' : formatDate(date)}</span>
      <button type="button" onClick={() => onChange(shiftDate(date, 1))} aria-label="Next day">
        <ChevronRight className="size-4" aria-hidden="true" />
      </button>
      {date !== today && (
        <button type="button" className="k-link" onClick={() => onChange(today)}>
          Today
        </button>
      )}
    </div>
  );
}

function Macro({ label, value, target }: { label: string; value: number; target: number | null }) {
  const share = percent(value, target);
  return (
    <Meter
      label={
        <>
          {label}{' '}
          <Num>
            {formatGrams(value)}
            {target !== null && <>/{formatGrams(target)}</>} g
          </Num>
        </>
      }
      value={value}
      max={target ?? Math.max(value, 1)}
      tone={share !== null && share > 100 ? 'late' : undefined}
    />
  );
}

function Macros({ day }: { day: FoodDay }) {
  const t: FoodTargets = day.targets;
  return (
    <div className="k-macros">
      <Macro label="Protein" value={day.totals.protein_g} target={t?.protein_g ?? null} />
      <Macro label="Fat" value={day.totals.fat_g} target={t?.fat_g ?? null} />
      <Macro label="Carbs" value={day.totals.carbs_g} target={t?.carbs_g ?? null} />
    </div>
  );
}

/** The diary of one day: the totals against the goals and the four meals. */
export function FoodDayPage() {
  const [date, setDate] = useDayParam();
  const day = useFoodDay(date);
  const [adding, setAdding] = useState<{ meal: FoodMeal; tab: 'search' | 'describe' } | null>(null);
  const [editing, setEditing] = useState<FoodEntry | null>(null);
  const d = day.data;
  const t = d?.targets;
  const eaten = d?.totals.kcal ?? 0;

  return (
    <FoodFrame
      title="Food"
      headline={
        !d ? (
          'Food'
        ) : t ? (
          <>
            <Num>{formatKcal(eaten)}</Num> of <Num>{formatKcal(t.kcal)}</Num> kcal
          </>
        ) : (
          <>
            <Num>{formatKcal(eaten)}</Num> kcal eaten
          </>
        )
      }
      sub={
        d && t ? (
          <>
            {eaten <= t.kcal ? (
              <>
                <Num>{formatKcal(t.kcal - eaten)}</Num> kcal left
              </>
            ) : (
              <>
                <Num>{formatKcal(eaten - t.kcal)}</Num> kcal over the goal
              </>
            )}
            {d.active_kcal !== null && (
              <>
                {' '}
                · balance <Num>{formatKcal(t.kcal - eaten + d.active_kcal)}</Num> with{' '}
                <Num>{formatKcal(d.active_kcal)}</Num> active
              </>
            )}
          </>
        ) : d ? (
          <>
            <Link to="/food/goals" className="k-link">
              Set your goals
            </Link>{' '}
            to see how the day compares.
          </>
        ) : undefined
      }
      actions={
        <>
          <DateRow date={date} onChange={setDate} />
          <span className="k-grow" />
          <button
            type="button"
            className="k-btn k-solid"
            onClick={() => setAdding({ meal: mealByHour(new Date().getHours()), tab: 'search' })}
          >
            Add
          </button>
        </>
      }
    >
      {day.isPending ? (
        <Placeholder />
      ) : day.isError ? (
        <LoadError error={day.error} onRetry={() => void day.refetch()} />
      ) : (
        <>
          {t && <Meter value={eaten} max={t.kcal} tone={eaten > t.kcal ? 'late' : undefined} />}
          <Macros day={day.data} />
          <button
            type="button"
            className="k-capture"
            onClick={() => setAdding({ meal: mealByHour(new Date().getHours()), tab: 'describe' })}
          >
            Describe a meal, e.g. two eggs and toast
            <span aria-hidden="true">+</span>
          </button>
          {MEALS.map((m) => {
            const entries = day.data.meals[m.value];
            const kcal = entries.reduce((n, e) => n + e.kcal, 0);
            return (
              <Group
                key={m.value}
                label={
                  <>
                    {m.label}{' '}
                    <button
                      type="button"
                      className="k-link k-add"
                      onClick={() => setAdding({ meal: m.value, tab: 'search' })}
                      aria-label={`Add to ${m.label}`}
                    >
                      + add
                    </button>
                  </>
                }
                total={entries.length > 0 ? <Num>{formatKcal(kcal)} kcal</Num> : undefined}
              >
                {entries.length === 0 ? (
                  <p className="k-empty">Nothing logged.</p>
                ) : (
                  entries.map((e) => <EntryRow key={e.id} entry={e} onOpen={() => setEditing(e)} />)
                )}
              </Group>
            );
          })}
        </>
      )}
      {adding && day.data && (
        <Modal onClose={() => setAdding(null)} className="max-w-xl">
          <AddEntry
            date={date}
            meal={adding.meal}
            initialTab={adding.tab}
            onClose={() => setAdding(null)}
          />
        </Modal>
      )}
      {editing && (
        <Modal onClose={() => setEditing(null)} className="max-w-md">
          <EntryEditor entry={editing} onClose={() => setEditing(null)} />
        </Modal>
      )}
    </FoodFrame>
  );
}

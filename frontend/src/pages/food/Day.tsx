import { useState } from 'react';
import { ChevronLeft, ChevronRight, Plus } from 'lucide-react';
import { Link, useSearchParams } from 'react-router';

import { Modal } from '@/components/Modal';
import { useFoodDay } from '@/hooks/useFood';
import {
  MEALS,
  formatDate,
  formatGrams,
  formatKcal,
  parseDateParam,
  percent,
  shiftDate,
  todayLocal,
  type FoodDay,
  type FoodEntry,
  type FoodMeal,
  type FoodTargets,
} from '@/lib/food';
import { cn } from '@/lib/utils';

import { cardClass, iconButton, secondaryButton } from '../workout/styles';
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
    <div className="flex flex-wrap items-center gap-2">
      <button
        type="button"
        onClick={() => onChange(shiftDate(date, -1))}
        className={iconButton}
        aria-label="Previous day"
      >
        <ChevronLeft className="size-5" aria-hidden="true" />
      </button>
      <p className="min-w-28 text-center text-theme-sm font-medium text-gray-800 dark:text-white/90">
        {date === today ? 'Today' : formatDate(date)}
      </p>
      <button
        type="button"
        onClick={() => onChange(shiftDate(date, 1))}
        className={iconButton}
        aria-label="Next day"
      >
        <ChevronRight className="size-5" aria-hidden="true" />
      </button>
      {date !== today && (
        <button
          type="button"
          onClick={() => onChange(today)}
          className={cn(secondaryButton, 'px-3 py-2')}
        >
          Today
        </button>
      )}
    </div>
  );
}

/** Eaten against the target, as a ring. */
function Ring({ value, target }: { value: number; target: number }) {
  const r = 52;
  const c = 2 * Math.PI * r;
  const share = Math.min(1, target > 0 ? value / target : 0);
  const over = value > target;
  return (
    <svg viewBox="0 0 120 120" className="size-32 shrink-0" role="img" aria-label="Calories ring">
      <circle
        cx="60"
        cy="60"
        r={r}
        className="fill-none stroke-gray-100 dark:stroke-white/10"
        strokeWidth="10"
      />
      <circle
        cx="60"
        cy="60"
        r={r}
        className={cn('fill-none', over ? 'stroke-error-500' : 'stroke-brand-500')}
        strokeWidth="10"
        strokeLinecap="round"
        strokeDasharray={c}
        strokeDashoffset={c * (1 - share)}
        transform="rotate(-90 60 60)"
      />
      <text
        x="60"
        y="56"
        textAnchor="middle"
        className="fill-gray-800 text-[20px] font-semibold tabular-nums dark:fill-white"
      >
        {formatKcal(value)}
      </text>
      <text x="60" y="76" textAnchor="middle" className="fill-gray-500 text-[11px]">
        of {formatKcal(target)}
      </text>
    </svg>
  );
}

function MacroBar({
  label,
  value,
  target,
}: {
  label: string;
  value: number;
  target: number | null;
}) {
  const share = percent(value, target);
  return (
    <div>
      <div className="flex items-baseline justify-between text-theme-xs">
        <span className="text-gray-500 dark:text-gray-400">{label}</span>
        <span className="tabular-nums text-gray-800 dark:text-white/90">
          {formatGrams(value)}
          {target !== null && <span className="text-gray-500"> / {formatGrams(target)} g</span>}
        </span>
      </div>
      <div className="mt-1 h-2 overflow-hidden rounded-full bg-gray-100 dark:bg-white/10">
        <div
          className={cn(
            'h-full rounded-full',
            share !== null && share > 100 ? 'bg-error-500' : 'bg-brand-500',
          )}
          style={{ width: `${Math.min(100, share ?? 0)}%` }}
        />
      </div>
    </div>
  );
}

function Summary({ day }: { day: FoodDay }) {
  const t: FoodTargets = day.targets;
  const eaten = day.totals.kcal;
  return (
    <div className={cn(cardClass, 'flex flex-col gap-5 sm:flex-row sm:items-center')}>
      {t ? (
        <>
          <Ring value={eaten} target={t.kcal} />
          <div className="flex-1">
            <p className="text-theme-sm text-gray-500 dark:text-gray-400">
              {eaten <= t.kcal
                ? `${formatKcal(t.kcal - eaten)} kcal left`
                : `${formatKcal(eaten - t.kcal)} kcal over the goal`}
              {day.active_kcal !== null && (
                <span>
                  {' '}
                  · balance {formatKcal(t.kcal - eaten + day.active_kcal)} with{' '}
                  {formatKcal(day.active_kcal)} active
                </span>
              )}
            </p>
            <div className="mt-3 grid gap-3 sm:grid-cols-3">
              <MacroBar label="Protein" value={day.totals.protein_g} target={t.protein_g} />
              <MacroBar label="Fat" value={day.totals.fat_g} target={t.fat_g} />
              <MacroBar label="Carbs" value={day.totals.carbs_g} target={t.carbs_g} />
            </div>
          </div>
        </>
      ) : (
        <div className="flex-1">
          <p className="text-2xl font-semibold text-gray-800 tabular-nums dark:text-white/90">
            {formatKcal(eaten)} kcal
          </p>
          <p className="mt-1 text-theme-sm text-gray-500 dark:text-gray-400">
            <Link to="/food/goals" className="text-brand-500 hover:underline">
              Set your goals
            </Link>{' '}
            to see how the day compares.
          </p>
          <div className="mt-3 grid gap-3 sm:grid-cols-3">
            <MacroBar label="Protein" value={day.totals.protein_g} target={null} />
            <MacroBar label="Fat" value={day.totals.fat_g} target={null} />
            <MacroBar label="Carbs" value={day.totals.carbs_g} target={null} />
          </div>
        </div>
      )}
    </div>
  );
}

function MealCard({
  meal,
  entries,
  onAdd,
  onOpen,
}: {
  meal: FoodMeal;
  entries: FoodEntry[];
  onAdd: () => void;
  onOpen: (entry: FoodEntry) => void;
}) {
  const kcal = entries.reduce((n, e) => n + e.kcal, 0);
  const label = MEALS.find((m) => m.value === meal)?.label ?? meal;
  return (
    <section className={cardClass} aria-labelledby={`meal-${meal}`}>
      <div className="flex items-center justify-between gap-3">
        <h2
          id={`meal-${meal}`}
          className="text-base font-semibold text-gray-800 dark:text-white/90"
        >
          {label}
          {entries.length > 0 && (
            <span className="ms-2 text-theme-sm font-normal text-gray-500 tabular-nums">
              {formatKcal(kcal)} kcal
            </span>
          )}
        </h2>
        <button
          type="button"
          onClick={onAdd}
          className={cn(secondaryButton, 'px-3 py-2')}
          aria-label={`Add to ${label}`}
        >
          <Plus className="size-4" aria-hidden="true" />
          Add
        </button>
      </div>
      {entries.length === 0 ? (
        <p className="mt-3 text-theme-sm text-gray-500 dark:text-gray-400">Nothing logged.</p>
      ) : (
        <ul className="mt-3 divide-y divide-gray-100 dark:divide-white/5">
          {entries.map((e) => (
            <EntryRow key={e.id} entry={e} onOpen={() => onOpen(e)} />
          ))}
        </ul>
      )}
    </section>
  );
}

/** The diary of one day: the totals against the goals and the four meals. */
export function FoodDayPage() {
  const [date, setDate] = useDayParam();
  const day = useFoodDay(date);
  const [adding, setAdding] = useState<FoodMeal | null>(null);
  const [editing, setEditing] = useState<FoodEntry | null>(null);

  return (
    <FoodFrame title="Food" actions={<DateRow date={date} onChange={setDate} />}>
      {day.isPending ? (
        <>
          <Placeholder className="h-44" />
          <Placeholder className="h-28" />
        </>
      ) : day.isError ? (
        <LoadError error={day.error} onRetry={() => void day.refetch()} />
      ) : (
        <>
          <Summary day={day.data} />
          <div className="grid gap-4 md:grid-cols-2 md:gap-6">
            {MEALS.map((m) => (
              <MealCard
                key={m.value}
                meal={m.value}
                entries={day.data.meals[m.value]}
                onAdd={() => setAdding(m.value)}
                onOpen={setEditing}
              />
            ))}
          </div>
        </>
      )}
      {adding && day.data && (
        <Modal onClose={() => setAdding(null)} className="max-w-xl">
          <AddEntry date={date} meal={adding} onClose={() => setAdding(null)} />
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

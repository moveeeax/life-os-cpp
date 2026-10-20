import { useState } from 'react';

import { MetricPicker } from '@/components/MetricPicker';
import { useSaveHabit } from '@/hooks/useHabits';
import { useMetrics } from '@/hooks/useMetrics';
import {
  habitBody,
  habitForm,
  habitProblem,
  type Habit,
  type HabitForm,
  type SourceInfo,
} from '@/lib/habits';
import { entryOf } from '@/lib/metrics';
import { Sheet } from '@/shell/kit';

/** New habit, or edit one: name, how often, what ticks it, the goal, a daily target. */
export function HabitSheet({ habit, onClose }: { habit?: Habit; onClose: () => void }) {
  const [f, setF] = useState<HabitForm>(() => habitForm(habit));
  const [problem, setProblem] = useState<string | null>(null);
  const save = useSaveHabit(habit?.id ?? null, onClose);
  const metrics = useMetrics();
  const set = (k: keyof HabitForm) => (e: { target: { value: string } }) =>
    setF({ ...f, [k]: e.target.value });
  const manual = f.source === 'manual';
  const entry = manual ? undefined : entryOf(metrics.data, f.source);
  const info: SourceInfo | undefined = entry && { unit: entry.unit, presence: entry.presence };
  const unit = manual ? '' : (entry?.unit ?? '');
  const goalOptional = manual || (info?.presence ?? false);
  return (
    <Sheet title={habit ? 'Edit the habit' : 'New habit'} onClose={onClose} width={460}>
      <form
        className="k-form"
        onSubmit={(e) => {
          e.preventDefault();
          const wrong = habitProblem(f, info);
          setProblem(wrong);
          if (!wrong) save.mutate(habitBody(f, info));
        }}
      >
        <label className="k-field">
          <span>Name</span>
          <input
            required
            maxLength={80}
            placeholder="Meditation"
            value={f.name}
            onChange={set('name')}
          />
        </label>
        <div className="k-field">
          <span id="habit-often">How often</span>
          <div className="h-often">
            <div className="k-seg" role="radiogroup" aria-labelledby="habit-often">
              {(['daily', 'weekly'] as const).map((s) => (
                <button
                  key={s}
                  type="button"
                  role="radio"
                  aria-checked={f.schedule === s}
                  className={f.schedule === s ? 'k-seg-on' : undefined}
                  onClick={() => setF({ ...f, schedule: s })}
                >
                  {s === 'daily' ? 'Every day' : 'Times a week'}
                </button>
              ))}
            </div>
            {f.schedule === 'weekly' && (
              <label className="h-per-week">
                <select value={f.per_week} onChange={set('per_week')} aria-label="Times a week">
                  {[1, 2, 3, 4, 5, 6, 7].map((n) => (
                    <option key={n} value={String(n)}>
                      {n}
                    </option>
                  ))}
                </select>
                <span>a week</span>
              </label>
            )}
          </div>
        </div>
        <div className="k-field">
          <label htmlFor="habit-source">Ticked by</label>
          <MetricPicker
            id="habit-source"
            value={manual ? null : f.source}
            onChange={(s) => setF({ ...f, source: s ?? 'manual' })}
            manual="By hand"
            exclude={habit ? [`habits:${habit.id}`] : undefined}
          />
        </div>
        {!manual && (
          <div className="k-field">
            <span id="habit-direction">The day counts at</span>
            <div className="h-often">
              <div className="k-seg" role="radiogroup" aria-labelledby="habit-direction">
                {(['at_least', 'at_most'] as const).map((d) => (
                  <button
                    key={d}
                    type="button"
                    role="radio"
                    aria-checked={f.direction === d}
                    className={f.direction === d ? 'k-seg-on' : undefined}
                    onClick={() => setF({ ...f, direction: d })}
                  >
                    {d === 'at_least' ? 'At least' : 'At most'}
                  </button>
                ))}
              </div>
            </div>
          </div>
        )}
        <div className="k-fields">
          <label className="k-field">
            <span>
              {goalOptional ? 'Goal (optional)' : 'Goal'}
              {unit ? `, ${unit}` : ''}
            </span>
            <input
              inputMode="decimal"
              required={!goalOptional}
              placeholder={
                !manual && info?.presence
                  ? 'any record counts'
                  : f.source === 'fitness:sleep'
                    ? '8'
                    : f.source === 'fitness:steps'
                      ? '8000'
                      : ''
              }
              value={f.goal}
              onChange={set('goal')}
            />
          </label>
          {manual && (
            <label className="k-field">
              <span>Unit</span>
              <input maxLength={16} placeholder="min" value={f.unit} onChange={set('unit')} />
            </label>
          )}
        </div>
        {manual && (
          <label className="k-field">
            <span>Daily target (optional){f.unit.trim() ? `, ${f.unit.trim()}` : ''}</span>
            <input
              inputMode="decimal"
              placeholder="2000"
              value={f.target}
              onChange={set('target')}
            />
          </label>
        )}
        <p className="k-sub">
          {manual
            ? 'With a daily target, a day counts once what you add that day reaches it (water in ml).'
            : 'A tick by hand still counts on its day.'}
        </p>
        {(problem || save.error) && (
          <p role="alert" className="k-error">
            {problem ?? save.error}
          </p>
        )}
        <div className="k-dlg-actions">
          <button type="button" className="k-btn" onClick={onClose}>
            Cancel
          </button>
          <button type="submit" className="k-btn k-solid" disabled={save.isPending}>
            {save.isPending ? 'Saving…' : habit ? 'Save' : 'Add'}
          </button>
        </div>
      </form>
    </Sheet>
  );
}

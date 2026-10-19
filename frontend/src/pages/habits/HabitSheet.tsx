import { useState } from 'react';

import { useSaveHabit } from '@/hooks/useHabits';
import {
  GOAL_UNIT,
  SOURCE_LABEL,
  habitBody,
  habitForm,
  habitProblem,
  type Habit,
  type HabitForm,
  type Source,
} from '@/lib/habits';
import { Sheet } from '@/shell/kit';

const SOURCES = Object.keys(SOURCE_LABEL) as Source[];

/** New habit, or edit one: name, how often, what ticks it, the goal, a daily target. */
export function HabitSheet({ habit, onClose }: { habit?: Habit; onClose: () => void }) {
  const [f, setF] = useState<HabitForm>(() => habitForm(habit));
  const [problem, setProblem] = useState<string | null>(null);
  const save = useSaveHabit(habit?.id ?? null, onClose);
  const set = (k: keyof HabitForm) => (e: { target: { value: string } }) =>
    setF({ ...f, [k]: e.target.value });
  const needsGoal = f.source === 'sleep' || f.source === 'steps' || f.source === 'active_kcal';
  const unit = GOAL_UNIT[f.source];
  return (
    <Sheet title={habit ? 'Edit the habit' : 'New habit'} onClose={onClose} width={460}>
      <form
        className="k-form"
        onSubmit={(e) => {
          e.preventDefault();
          const wrong = habitProblem(f);
          setProblem(wrong);
          if (!wrong) save.mutate(habitBody(f));
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
        <label className="k-field">
          <span>Ticked by</span>
          <select value={f.source} onChange={set('source')}>
            {SOURCES.map((s) => (
              <option key={s} value={s}>
                {SOURCE_LABEL[s]}
              </option>
            ))}
          </select>
        </label>
        {f.source !== 'workout' && (
          <div className="k-fields">
            <label className="k-field">
              <span>
                {needsGoal ? 'Goal' : 'Goal (optional)'}
                {unit ? `, ${unit}` : ''}
              </span>
              <input
                inputMode="decimal"
                required={needsGoal}
                placeholder={f.source === 'sleep' ? '8' : f.source === 'steps' ? '8000' : ''}
                value={f.goal}
                onChange={set('goal')}
              />
            </label>
            {f.source === 'manual' && (
              <label className="k-field">
                <span>Unit</span>
                <input maxLength={16} placeholder="min" value={f.unit} onChange={set('unit')} />
              </label>
            )}
          </div>
        )}
        {f.source === 'manual' && (
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
          A source from Mi needs a goal; a Training session means any finished session that day.
          With a daily target, a day counts once what you add that day reaches it (water in ml).
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

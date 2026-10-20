import { useState, type FormEvent } from 'react';

import { useErrorToast } from '@/hooks/useErrorToast';
import { MetricPicker } from '@/components/MetricPicker';
import { Modal } from '@/components/Modal';
import { useMetrics } from '@/hooks/useMetrics';
import { useDeleteGoal, useGoal, useGoals, useSaveGoal } from '@/hooks/useGoals';
import { formatValue, KIND_LABEL, paceWord, weeklyPace, type Goal, type Tone } from '@/lib/goals';
import { parseAmount } from '@/lib/amounts';
import { formatDay } from '@/lib/dates';
import { goalBody, goalForm, goalPatch, type GoalFormValues } from '@/lib/goals/bodies';
import { entryOf } from '@/lib/metrics';
import { AREAS, areaOf, localToday } from '@/lib/tasks';
import { cn } from '@/lib/utils';

import {
  inputClass,
  labelClass,
  modalPanel,
  primaryButton,
  secondaryButton,
} from '@/shell/kit/styles';
import { Facts as KitFacts, Group, Meter, Num, SectionFrame } from '@/shell/kit';
import { LoadError as KitLoadError, Placeholder } from '@/shell/kit';

const LoadError = ({ error, onRetry }: { error: unknown; onRetry: () => void }) => (
  <KitLoadError error={error} onRetry={onRetry} module="goals" />
);
import { BinaryPart, CountPart, NumberPart, StepsPart } from './parts';

const TONE: Record<Tone, string> = {
  ok: 'k-pos',
  behind: 'k-warn-text',
  stale: 'k-muted',
  fail: 'k-error-text',
};
const shortDate = (s: string, today: string) =>
  formatDay(s, { locale: 'en-GB', yearUnless: today });

/** Three numbers where they say something: now, expected today, the pace to make it. */
function Facts({ goal }: { goal: Goal }) {
  const p = goal.progress;
  let facts: [string, string][] = [];
  if (goal.kind === 'number') {
    const pace = weeklyPace(p.progress, p.per_week, goal.unit);
    facts = [
      [formatValue(p.current ?? 0, goal.unit), 'now'],
      [formatValue(p.expected ?? 0, goal.unit), 'expected today'],
      [pace.value, pace.label],
    ];
  } else if (goal.kind === 'steps') {
    facts = [
      [`${p.done ?? 0} of ${p.total ?? 0}`, 'tasks done'],
      [`${Math.round(p.elapsed * 100)}%`, 'of the time gone'],
      [`${p.days_left} d`, 'left'],
    ];
  } else if (goal.kind === 'count') {
    facts = [
      [`${p.done ?? 0} of ${p.target ?? 0}`, 'done'],
      [String(Math.round((p.expected ?? 0) * 10) / 10), 'expected by today'],
      [`${p.days_left} d`, 'left'],
    ];
  }
  if (!facts.length) return null;
  return <KitFacts items={facts.map(([v, l]) => ({ label: l, value: <Num>{v}</Num> }))} />;
}

function GoalMore({ goal, today }: { goal: Goal; today: string }) {
  const detail = useGoal(goal.id, today);
  const save = useSaveGoal(goal.id);
  const remove = useDeleteGoal();
  const [editing, setEditing] = useState(false);
  useErrorToast(save.error ?? remove.error);
  const d = detail.data;
  return (
    <div className="k-row-more">
      <span className="k-sub">
        {KIND_LABEL[goal.kind]}
        {goal.unit ? ` · ${goal.unit}` : ''}
      </span>
      {goal.why && <p className="k-quote">{goal.why}</p>}
      <Facts goal={goal} />
      {detail.isError ? (
        <LoadError error={detail.error} onRetry={() => void detail.refetch()} />
      ) : !d ? (
        <Placeholder className="h-32" />
      ) : d.kind === 'number' ? (
        <NumberPart goal={d} today={today} />
      ) : d.kind === 'steps' ? (
        <StepsPart goal={d} today={today} />
      ) : d.kind === 'count' ? (
        <CountPart goal={d} today={today} />
      ) : (
        <BinaryPart goal={d} today={today} />
      )}
      <div className="k-actions">
        <button type="button" onClick={() => setEditing(true)} className="k-link">
          Edit
        </button>
        {goal.kind !== 'binary' && goal.status === 'active' && (
          <button type="button" onClick={() => save.mutate({ status: 'done' })} className="k-link">
            Mark done
          </button>
        )}
        {goal.status === 'active' && (
          <button
            type="button"
            onClick={() => save.mutate({ status: 'dropped' })}
            className="k-link"
          >
            Drop
          </button>
        )}
        {goal.status !== 'active' && (
          <button
            type="button"
            onClick={() => remove.mutate(goal.id)}
            className="k-link k-error-text"
          >
            Delete
          </button>
        )}
      </div>
      {editing && (
        <Modal onClose={() => setEditing(false)} className="max-w-lg">
          <GoalForm goal={goal} onClose={() => setEditing(false)} />
        </Modal>
      )}
    </div>
  );
}

function GoalRow({
  goal,
  today,
  open,
  onToggle,
}: {
  goal: Goal;
  today: string;
  open: boolean;
  onToggle: () => void;
}) {
  const pace = paceWord(goal);
  const progress = goal.kind === 'binary' ? goal.progress.elapsed : (goal.progress.progress ?? 0);
  return (
    <div className="k-row">
      <button type="button" onClick={onToggle} aria-expanded={open} className="k-row-head">
        <span className="k-row-top">
          <span className={cn('k-dot', areaOf(goal.area).dot)} aria-hidden="true" />
          <span className="k-row-title" data-num-ok>
            {goal.title}
          </span>
          <span className="k-row-right k-muted">{shortDate(goal.due, today)}</span>
        </span>
        <span className="k-row-sub k-goal-line">
          <Meter value={progress} max={1} />
          <span className={TONE[pace.tone]}>
            <Num>{pace.text}</Num>
          </span>
        </span>
      </button>
      {open && <GoalMore goal={goal} today={today} />}
    </div>
  );
}

/** A new goal, or an edit of @p goal: the edit sends only what changed (kind stays). */
export function GoalForm({ goal, onClose }: { goal?: Goal; onClose: () => void }) {
  const today = localToday();
  const [f, setF] = useState<GoalFormValues>(() =>
    goal
      ? goalForm(goal)
      : {
          title: '',
          area: 'health',
          kind: 'number',
          due: '',
          why: '',
          from: '',
          to: '',
          unit: '',
          count: '',
          source: null,
        },
  );
  const set =
    (k: keyof GoalFormValues) =>
    (e: { target: { value: string } }): void =>
      setF((s) => ({ ...s, [k]: e.target.value }));
  const metrics = useMetrics(f.kind === 'number');
  const save = useSaveGoal(goal?.id ?? null, onClose);
  const kind = f.kind;
  // A number goal may start or end below zero; a count is a whole number of times.
  const fromValue = parseAmount(f.from, { signed: true });
  const toValue = parseAmount(f.to, { signed: true });
  const countValue = parseAmount(f.count);
  // A goal past its date can still be edited without moving the date.
  const dueOk = goal && f.due === goal.due ? true : f.due > today;
  const ok =
    f.title.trim() !== '' &&
    dueOk &&
    (kind !== 'number' || (fromValue !== null && toValue !== null && fromValue !== toValue)) &&
    (kind !== 'count' || (countValue !== null && Number.isInteger(countValue) && countValue >= 1));
  const pickSource = (source: string | null) =>
    setF((s) => {
      // The unit follows the series unless one was typed.
      const unit = entryOf(metrics.data, source)?.unit;
      return { ...s, source, unit: s.unit.trim() === '' && unit ? unit : s.unit };
    });
  const submit = (e: FormEvent) => {
    e.preventDefault();
    if (!ok) return;
    if (!goal) {
      save.mutate(goalBody({ ...f, today }));
      return;
    }
    const patch = goalPatch(goal, f);
    if (Object.keys(patch).length === 0) onClose();
    else save.mutate(patch);
  };
  return (
    <form onSubmit={submit} className={modalPanel} aria-labelledby="goal-form-title">
      <h2 id="goal-form-title" className="text-lg font-semibold">
        {goal ? 'Edit the goal' : 'New goal'}
      </h2>
      <div className="mt-4 grid grid-cols-1 gap-4 sm:grid-cols-2">
        <div className="sm:col-span-2">
          <label htmlFor="goal-title" className={labelClass}>
            Title
          </label>
          <input
            id="goal-title"
            value={f.title}
            onChange={set('title')}
            maxLength={200}
            className={inputClass}
          />
        </div>
        <div>
          <label htmlFor="goal-kind" className={labelClass}>
            Kind
          </label>
          <select
            id="goal-kind"
            value={kind}
            disabled={goal !== undefined}
            onChange={set('kind')}
            className={inputClass}
          >
            <option value="number">A number (weight, savings)</option>
            <option value="steps">Steps by section (a course)</option>
            <option value="count">A count of things (books)</option>
            <option value="binary">Pass or fail (an exam)</option>
          </select>
        </div>
        <div>
          <label htmlFor="goal-area" className={labelClass}>
            Area
          </label>
          <select id="goal-area" value={f.area} onChange={set('area')} className={inputClass}>
            {AREAS.map((a) => (
              <option key={a.key} value={a.key}>
                {a.label}
              </option>
            ))}
          </select>
        </div>
        <div>
          <label htmlFor="goal-due" className={labelClass}>
            By
          </label>
          <input
            id="goal-due"
            type="date"
            value={f.due}
            min={today}
            onChange={set('due')}
            className={inputClass}
          />
        </div>
        {kind === 'number' && (
          <>
            <div className="sm:col-span-2">
              <label htmlFor="goal-source" className={labelClass}>
                Numbers from
              </label>
              <MetricPicker
                id="goal-source"
                value={f.source ?? null}
                onChange={pickSource}
                manual="Manual check-ins"
                className={inputClass}
              />
            </div>
            <div>
              <label htmlFor="goal-unit" className={labelClass}>
                Unit
              </label>
              <input
                id="goal-unit"
                value={f.unit}
                onChange={set('unit')}
                maxLength={20}
                placeholder="kg"
                className={inputClass}
              />
            </div>
            <div>
              <label htmlFor="goal-from" className={labelClass}>
                {goal ? 'Start' : 'Now'}
              </label>
              <input
                id="goal-from"
                value={f.from}
                onChange={set('from')}
                inputMode="decimal"
                className={inputClass}
              />
            </div>
            <div>
              <label htmlFor="goal-to" className={labelClass}>
                Target
              </label>
              <input
                id="goal-to"
                value={f.to}
                onChange={set('to')}
                inputMode="decimal"
                className={inputClass}
              />
            </div>
          </>
        )}
        {kind === 'count' && (
          <div>
            <label htmlFor="goal-count" className={labelClass}>
              How many
            </label>
            <input
              id="goal-count"
              value={f.count}
              onChange={set('count')}
              inputMode="numeric"
              className={inputClass}
            />
          </div>
        )}
        <div className="sm:col-span-2">
          <label htmlFor="goal-why" className={labelClass}>
            Why (optional)
          </label>
          <input
            id="goal-why"
            value={f.why}
            onChange={set('why')}
            maxLength={300}
            className={inputClass}
          />
        </div>
      </div>
      {save.error && (
        <p role="alert" className="mt-3 text-theme-sm k-error-text">
          {save.error}
        </p>
      )}
      <div className="mt-5 flex justify-end gap-3">
        <button type="button" onClick={onClose} className={secondaryButton}>
          Cancel
        </button>
        <button type="submit" disabled={save.isPending || !ok} className={primaryButton}>
          {save.isPending ? 'Saving…' : goal ? 'Save' : 'Create'}
        </button>
      </div>
    </form>
  );
}

/** The person's goals: active ones open in place; done and dropped folded. */
export function GoalsPage() {
  const today = localToday();
  const active = useGoals('active', today);
  const done = useGoals('done', today);
  const dropped = useGoals('dropped', today);
  const [open, setOpen] = useState<Set<string>>(new Set());
  const [creating, setCreating] = useState(false);
  const toggle = (id: string) =>
    setOpen((s) => {
      const n = new Set(s);
      if (n.has(id)) n.delete(id);
      else n.add(id);
      return n;
    });
  const closed = (done.data ?? []).concat(dropped.data ?? []);
  return (
    <SectionFrame
      eyebrow="Goals"
      headline={
        active.data ? (
          <>
            <Num>{active.data.length}</Num> active
          </>
        ) : (
          'Goals'
        )
      }
      tools={
        <>
          <span className="k-grow" />
          <button type="button" onClick={() => setCreating(true)} className="k-btn k-solid">
            New goal
          </button>
        </>
      }
    >
      {active.isError ? (
        <LoadError error={active.error} onRetry={() => void active.refetch()} />
      ) : !active.data ? (
        <Placeholder />
      ) : active.data.length === 0 ? (
        <p className="k-empty">
          No goals yet. A goal with a date and a way to measure it is a good start.
        </p>
      ) : (
        <Group label="Active">
          {active.data.map((g) => (
            <GoalRow
              key={g.id}
              goal={g}
              today={today}
              open={open.has(g.id)}
              onToggle={() => toggle(g.id)}
            />
          ))}
        </Group>
      )}
      {closed.length > 0 && (
        <details className="k-details">
          <summary>
            Done and dropped (<Num>{closed.length}</Num>)
          </summary>
          <div className="k-group-rows">
            {closed.map((g) => (
              <GoalRow
                key={g.id}
                goal={g}
                today={today}
                open={open.has(g.id)}
                onToggle={() => toggle(g.id)}
              />
            ))}
          </div>
        </details>
      )}
      {creating && (
        <Modal onClose={() => setCreating(false)} className="max-w-lg">
          <GoalForm onClose={() => setCreating(false)} />
        </Modal>
      )}
    </SectionFrame>
  );
}

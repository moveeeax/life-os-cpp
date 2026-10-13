import { useState, type FormEvent } from 'react';

import { useErrorToast } from '@/hooks/useErrorToast';
import { Modal } from '@/components/Modal';
import { useDeleteGoal, useGoal, useGoals, useSaveGoal } from '@/hooks/useGoals';
import {
  formatValue,
  KIND_LABEL,
  paceWord,
  type Goal,
  type GoalKind,
  type Tone,
} from '@/lib/goals';
import { goalBody } from '@/lib/goals/bodies';
import { AREAS, areaOf, localToday, type Area } from '@/lib/tasks';
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
  new Date(`${s}T12:00:00Z`).toLocaleDateString('en-GB', {
    day: 'numeric',
    month: 'short',
    year: s.slice(0, 4) === today.slice(0, 4) ? undefined : 'numeric',
    timeZone: 'UTC',
  });

/** Three numbers where they say something: now, expected today, the pace to make it. */
function Facts({ goal }: { goal: Goal }) {
  const p = goal.progress;
  let facts: [string, string][] = [];
  if (goal.kind === 'number') {
    const perWeek = p.per_week ?? 0;
    facts = [
      [formatValue(p.current ?? 0, goal.unit), 'now'],
      [formatValue(p.expected ?? 0, goal.unit), 'expected today'],
      [
        `${perWeek > 0 ? '+' : '−'}${formatValue(Math.abs(perWeek), goal.unit)}`,
        'a week to make it',
      ],
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
  useErrorToast(save.error ?? remove.error);
  const d = detail.data;
  return (
    <div className="k-row-more">
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
        <span className="k-row-sub">{KIND_LABEL[goal.kind]}</span>
      </button>
      {open && <GoalMore goal={goal} today={today} />}
    </div>
  );
}

function GoalForm({ onClose }: { onClose: () => void }) {
  const today = localToday();
  const [kind, setKind] = useState<GoalKind>('number');
  const [title, setTitle] = useState('');
  const [area, setArea] = useState<Area>('health');
  const [due, setDue] = useState('');
  const [why, setWhy] = useState('');
  const [from, setFrom] = useState('');
  const [to, setTo] = useState('');
  const [unit, setUnit] = useState('');
  const [count, setCount] = useState('');
  const save = useSaveGoal(null, onClose);
  const num = (s: string) => Number(s.replace(',', '.'));
  const ok =
    title.trim() !== '' &&
    due > today &&
    (kind !== 'number' ||
      (from.trim() !== '' &&
        to.trim() !== '' &&
        Number.isFinite(num(from)) &&
        Number.isFinite(num(to)) &&
        num(from) !== num(to))) &&
    (kind !== 'count' || (Number.isInteger(num(count)) && num(count) >= 1));
  const submit = (e: FormEvent) => {
    e.preventDefault();
    if (!ok) return;
    save.mutate(goalBody({ title, area, kind, due, today, why, from, to, unit, count }));
  };
  return (
    <form onSubmit={submit} className={modalPanel} aria-labelledby="goal-form-title">
      <h2 id="goal-form-title" className="text-lg font-semibold">
        New goal
      </h2>
      <div className="mt-4 grid grid-cols-1 gap-4 sm:grid-cols-2">
        <div className="sm:col-span-2">
          <label htmlFor="goal-title" className={labelClass}>
            Title
          </label>
          <input
            id="goal-title"
            value={title}
            onChange={(e) => setTitle(e.target.value)}
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
            onChange={(e) => setKind(e.target.value as GoalKind)}
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
          <select
            id="goal-area"
            value={area}
            onChange={(e) => setArea(e.target.value as Area)}
            className={inputClass}
          >
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
            value={due}
            min={today}
            onChange={(e) => setDue(e.target.value)}
            className={inputClass}
          />
        </div>
        {kind === 'number' && (
          <>
            <div>
              <label htmlFor="goal-unit" className={labelClass}>
                Unit
              </label>
              <input
                id="goal-unit"
                value={unit}
                onChange={(e) => setUnit(e.target.value)}
                maxLength={20}
                placeholder="kg"
                className={inputClass}
              />
            </div>
            <div>
              <label htmlFor="goal-from" className={labelClass}>
                Now
              </label>
              <input
                id="goal-from"
                value={from}
                onChange={(e) => setFrom(e.target.value)}
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
                value={to}
                onChange={(e) => setTo(e.target.value)}
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
              value={count}
              onChange={(e) => setCount(e.target.value)}
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
            value={why}
            onChange={(e) => setWhy(e.target.value)}
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
          {save.isPending ? 'Saving…' : 'Create'}
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
      headline={active.data ? <span data-num-ok>{active.data.length} active</span> : 'Goals'}
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
            Done and dropped (<span data-num-ok>{closed.length}</span>)
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

import { useState, type FormEvent } from 'react';

import { Modal } from '@/components/Modal';
import { useDeleteGoal, useGoal, useGoals, useSaveGoal } from '@/hooks/useGoals';
import {
  formatValue,
  KIND_LABEL,
  paceWord,
  type Goal,
  type GoalInput,
  type GoalKind,
  type Tone,
} from '@/lib/goals';
import { AREAS, areaOf, localToday, type Area } from '@/lib/tasks';
import { cn } from '@/lib/utils';

import {
  inputClass,
  labelClass,
  modalPanel,
  primaryButton,
  secondaryButton,
} from '../workout/styles';
import { LoadError, Placeholder } from '../money/frame';
import { BinaryPart, CountPart, NumberPart, StepsPart } from './parts';

const TONE: Record<Tone, string> = {
  ok: 'text-success-600',
  behind: 'text-warning-600',
  stale: 'text-gray-400',
  fail: 'text-error-500',
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
  return (
    <div className="grid grid-cols-3 gap-3">
      {facts.map(([v, l]) => (
        <div key={l}>
          <div className="text-lg font-semibold text-gray-800 tabular-nums dark:text-white/90">
            {v}
          </div>
          <div className="text-theme-xs text-gray-500">{l}</div>
        </div>
      ))}
    </div>
  );
}

function GoalMore({ goal, today }: { goal: Goal; today: string }) {
  const detail = useGoal(goal.id, today);
  const save = useSaveGoal(goal.id);
  const remove = useDeleteGoal();
  const d = detail.data;
  return (
    <div className="grid gap-4 ps-4 pb-5">
      {goal.why && (
        <p className="border-s-2 border-gray-200 ps-3 text-theme-sm text-gray-500 dark:border-white/10">
          {goal.why}
        </p>
      )}
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
      <div className="flex gap-4 text-theme-sm">
        {goal.kind !== 'binary' && goal.status === 'active' && (
          <button
            type="button"
            onClick={() => save.mutate({ status: 'done' })}
            className="text-gray-500 hover:text-gray-800 dark:hover:text-gray-200"
          >
            Mark done
          </button>
        )}
        {goal.status === 'active' && (
          <button
            type="button"
            onClick={() => save.mutate({ status: 'dropped' })}
            className="text-gray-500 hover:text-gray-800 dark:hover:text-gray-200"
          >
            Drop
          </button>
        )}
        {goal.status !== 'active' && (
          <button type="button" onClick={() => remove.mutate(goal.id)} className="text-error-500">
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
    <li className="border-b border-gray-100 last:border-b-0 dark:border-white/5">
      <button
        type="button"
        onClick={onToggle}
        aria-expanded={open}
        className="grid w-full gap-2 py-3.5 text-start"
      >
        <span className="flex min-w-0 items-center gap-2.5">
          <span
            className={cn('size-[7px] shrink-0 rounded-full', areaOf(goal.area).dot)}
            aria-hidden="true"
          />
          <span className="min-w-0 flex-1 truncate text-theme-sm font-medium text-gray-800 dark:text-white/90">
            {goal.title}
          </span>
          <span className="rounded-md border border-gray-200 px-1.5 text-[11px] text-gray-400 dark:border-white/10">
            {KIND_LABEL[goal.kind]}
          </span>
          <span className="text-theme-xs whitespace-nowrap text-gray-400 tabular-nums">
            {shortDate(goal.due, today)}
          </span>
        </span>
        <span className="grid grid-cols-[minmax(0,1fr)_auto] items-center gap-3 ps-4">
          <span
            className="h-1.5 overflow-hidden rounded-full bg-gray-100 dark:bg-white/10"
            aria-label={`${Math.round(progress * 100)}%`}
          >
            <span
              className="block h-full rounded-full bg-brand-500"
              style={{ width: `${Math.round(progress * 100)}%` }}
            />
          </span>
          <span className={cn('text-theme-xs whitespace-nowrap tabular-nums', TONE[pace.tone])}>
            {pace.text}
          </span>
        </span>
      </button>
      {open && <GoalMore goal={goal} today={today} />}
    </li>
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
    const body: GoalInput = {
      title: title.trim(),
      area,
      kind,
      due,
      start_date: today,
      why: why.trim(),
    };
    if (kind === 'number')
      Object.assign(body, { start_value: num(from), target_value: num(to), unit: unit.trim() });
    if (kind === 'count') body.target_count = num(count);
    save.mutate(body);
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
        <p role="alert" className="mt-3 text-theme-sm text-error-500">
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
    <div className="mx-auto grid max-w-2xl gap-7">
      <div className="flex flex-wrap items-baseline justify-between gap-3">
        <h1 className="text-title-sm font-semibold text-gray-800 dark:text-white/90">Goals</h1>
        <button
          type="button"
          onClick={() => setCreating(true)}
          className="text-theme-sm font-medium text-brand-500"
        >
          New goal
        </button>
      </div>
      {active.isError ? (
        <LoadError error={active.error} onRetry={() => void active.refetch()} />
      ) : !active.data ? (
        <Placeholder className="h-40" />
      ) : active.data.length === 0 ? (
        <p className="text-theme-sm text-gray-400">
          No goals yet. A goal with a date and a way to measure it is a good start.
        </p>
      ) : (
        <section aria-label="Active">
          <h2 className="mb-1 text-theme-xs font-medium tracking-wider text-gray-400 uppercase">
            Active
          </h2>
          <ul>
            {active.data.map((g) => (
              <GoalRow
                key={g.id}
                goal={g}
                today={today}
                open={open.has(g.id)}
                onToggle={() => toggle(g.id)}
              />
            ))}
          </ul>
        </section>
      )}
      {closed.length > 0 && (
        <details>
          <summary className="cursor-pointer text-theme-sm text-gray-500">
            Done and dropped ({closed.length})
          </summary>
          <ul className="mt-2">
            {closed.map((g) => (
              <GoalRow
                key={g.id}
                goal={g}
                today={today}
                open={open.has(g.id)}
                onToggle={() => toggle(g.id)}
              />
            ))}
          </ul>
        </details>
      )}
      {creating && (
        <Modal onClose={() => setCreating(false)} className="max-w-lg">
          <GoalForm onClose={() => setCreating(false)} />
        </Modal>
      )}
    </div>
  );
}

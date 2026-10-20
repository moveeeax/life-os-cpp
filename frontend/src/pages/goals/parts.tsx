import { useState, type FormEvent } from 'react';

import { useErrorToast } from '@/hooks/useErrorToast';
import { ConfirmDialog } from '@/components/ConfirmDialog';

import { useAddMilestone, useAddSection, usePutCheckin, useSaveGoal } from '@/hooks/useGoals';
import { useMetrics } from '@/hooks/useMetrics';
import { useCreateTask, useUpdateTask } from '@/hooks/useTasks';
import { parseAmount } from '@/lib/amounts';
import { daysBetween, formatDay } from '@/lib/dates';
import {
  axisLabels,
  formatValue,
  goalChart,
  lastSourceDate,
  shelf,
  type GoalDetail,
} from '@/lib/goals';
import { entryOf, WHY_NOTE } from '@/lib/metrics';
import { dueLabel, type Task } from '@/lib/tasks';
import { cn } from '@/lib/utils';
import { Meter, Num } from '@/shell/kit';
import { inputClass } from '@/shell/kit/styles';

// Chart positions: an instant per day (noon, so a zone never shifts the day).
const day = (s: string) => Date.parse(`${s}T12:00:00Z`);
const shortDate = (s: string) => formatDay(s, { locale: 'en-GB' });
const compactInput = `${inputClass} k-compact`;
const ghost = 'k-link';
const STATUS: Record<Task['status'], string> = {
  done: 'done',
  in_progress: 'in progress',
  open: 'not started',
};

/**
 * The goal's points against the even-pace line, drawn to scale: the days of its
 * source as small dots, check-ins as hollow rings.
 */
function NumberChart({ goal, today }: { goal: GoalDetail; today: string }) {
  const W = 640;
  const H = 200;
  const P = { l: 46, r: 12, t: 12, b: 26 };
  const points = goalChart(goal);
  const from = goal.start_value ?? 0;
  const to = goal.target_value ?? 0;
  const values = points.map((p) => p.value).concat([from, to]);
  const lo = Math.min(...values);
  const hi = Math.max(...values);
  const pad = (hi - lo) * 0.08 || 1;
  const t0 = day(goal.start_date);
  const t1 = day(goal.due);
  const X = (d: string) => P.l + ((day(d) - t0) / (t1 - t0)) * (W - P.l - P.r);
  const Y = (v: number) => P.t + (1 - (v - lo + pad) / (hi - lo + 2 * pad)) * (H - P.t - P.b);
  const last = points[points.length - 1];
  const tick = (v: number) =>
    Math.abs(v) >= 10_000 ? `${Math.round(v / 1000)}k` : String(Math.round(v * 10) / 10);
  const todayIn = day(today) >= t0 && day(today) <= t1;
  const labels = axisLabels({
    start: { x: P.l, text: shortDate(goal.start_date) },
    end: { x: W - P.r, text: shortDate(goal.due) },
    today: todayIn ? X(today) : null,
  });
  return (
    <svg
      viewBox={`0 0 ${W} ${H}`}
      className="h-auto w-full"
      role="img"
      aria-label={
        goal.source
          ? 'The source and check-ins against the even pace'
          : 'Check-ins against the even pace'
      }
    >
      {[from, (from + to) / 2, to].map((v) => (
        <g key={v}>
          <line
            x1={P.l}
            x2={W - P.r}
            y1={Y(v)}
            y2={Y(v)}
            className="stroke-gray-200 dark:stroke-white/10"
          />
          <text
            x={P.l - 6}
            y={Y(v) + 4}
            textAnchor="end"
            fontSize="11"
            className="num fill-gray-400"
          >
            {tick(v)}
          </text>
        </g>
      ))}
      <line
        x1={X(goal.start_date)}
        y1={Y(from)}
        x2={X(goal.due)}
        y2={Y(to)}
        strokeDasharray="4 4"
        className="stroke-gray-400"
      />
      {todayIn && (
        <line
          x1={X(today)}
          x2={X(today)}
          y1={P.t}
          y2={H - P.b}
          className="stroke-gray-200 dark:stroke-white/10"
        />
      )}
      {labels.map((l) => (
        <text
          key={l.text === 'today' ? 'today' : l.anchor}
          x={l.x}
          y={H - 8}
          textAnchor={l.anchor}
          fontSize="11"
          className="fill-gray-400"
        >
          {l.text}
        </text>
      ))}
      <polyline
        points={points.map((p) => `${X(p.date)},${Y(p.value)}`).join(' ')}
        fill="none"
        strokeWidth="2"
        className="stroke-brand-500"
      />
      {points.map((p) =>
        p.from === 'checkin' ? (
          <circle
            key={p.date}
            cx={X(p.date)}
            cy={Y(p.value)}
            r="3.5"
            strokeWidth="1.5"
            data-from="checkin"
            className="fill-white stroke-brand-500 dark:fill-gray-900"
          />
        ) : (
          <circle
            key={p.date}
            cx={X(p.date)}
            cy={Y(p.value)}
            r={p.from === 'source' ? 2 : 3}
            data-from={p.from}
            className="fill-brand-500"
          />
        ),
      )}
      <circle
        cx={X(last.date)}
        cy={Y(last.value)}
        r="5"
        strokeWidth="2"
        className="fill-white stroke-brand-500 dark:fill-gray-900"
      />
    </svg>
  );
}

/** Where a sourced goal's numbers come from: "from Weight, last 19 Oct", or why there are none. */
function SourceLine({ goal }: { goal: GoalDetail }) {
  const metrics = useMetrics(goal.source !== null);
  if (!goal.source) return null;
  const label = entryOf(metrics.data, goal.source)?.label ?? goal.source;
  if (goal.source_why) return <p className="k-sub">{WHY_NOTE[goal.source_why]}</p>;
  const last = lastSourceDate(goal.points ?? []);
  return (
    <p className="k-sub">
      from {label}
      {last ? (
        <>
          , last <span data-num-ok>{shortDate(last)}</span>
        </>
      ) : (
        ', no days yet'
      )}
    </p>
  );
}

export function NumberPart({ goal, today }: { goal: GoalDetail; today: string }) {
  const [value, setValue] = useState('');
  const [date, setDate] = useState(today);
  // A sourced goal keeps the check-in form folded: its numbers come by themselves.
  const [byHand, setByHand] = useState(goal.source === null);
  const put = usePutCheckin(goal.id, () => setValue(''));
  // A check-in may be negative, like the goal's own start and target.
  const v = parseAmount(value, { signed: true });
  const submit = (e: FormEvent) => {
    e.preventDefault();
    if (v !== null && date) put.mutate({ value: v, date });
  };
  return (
    <div className="grid gap-3">
      <SourceLine goal={goal} />
      <NumberChart goal={goal} today={today} />
      {byHand ? (
        <form onSubmit={submit} className="k-inline-form">
          <input
            value={value}
            onChange={(e) => setValue(e.target.value)}
            inputMode="decimal"
            placeholder={goal.unit || 'value'}
            aria-label="Value"
            className={compactInput}
          />
          <input
            type="date"
            value={date}
            onChange={(e) => setDate(e.target.value)}
            aria-label="Date"
            className={compactInput}
          />
          <button
            type="submit"
            disabled={put.isPending}
            className="h-9 rounded-lg bg-brand-500 px-4 text-theme-sm font-medium text-white disabled:opacity-50"
          >
            Check in
          </button>
        </form>
      ) : (
        <div>
          <button type="button" onClick={() => setByHand(true)} className={ghost}>
            Add by hand
          </button>
        </div>
      )}
      {put.error && (
        <p role="alert" className="text-theme-sm k-error-text">
          {put.error}
        </p>
      )}
      {goal.checkins.length > 0 && (
        <p className="text-theme-xs k-muted tabular-nums">
          {goal.checkins.slice(0, 4).map((c, i) => (
            <span key={c.date}>
              {i > 0 && '  ·  '}
              {shortDate(c.date)} · <span className="num">{formatValue(c.value, goal.unit)}</span>
            </span>
          ))}
        </p>
      )}
    </div>
  );
}

/** A task line inside a goal: round check, title, status or done word. */
function GoalTask({ task, today }: { task: Task; today: string }) {
  const update = useUpdateTask(task.id);
  useErrorToast(update.error);
  const done = task.status === 'done';
  const due = done ? null : dueLabel(task.due, today);
  return (
    <li className="grid grid-cols-[18px_minmax(0,1fr)_auto] items-center gap-2.5 py-1 text-theme-sm">
      <button
        type="button"
        aria-label={done ? `Reopen: ${task.title}` : `Done: ${task.title}`}
        disabled={update.isPending}
        onClick={() => update.mutate({ status: done ? 'open' : 'done' })}
        className={cn(
          'size-4 rounded-full border-[1.5px]',
          done ? 'border-success-500 bg-success-500' : 'border-gray-400',
        )}
      />
      <span className={cn('truncate', done ? 'k-muted line-through' : '')} data-num-ok>
        {task.title}
      </span>
      <span
        className={cn(
          'text-theme-xs whitespace-nowrap',
          task.status === 'in_progress' ? 'text-brand-500' : 'k-muted',
        )}
      >
        {due ? due.text : STATUS[task.status]}
      </span>
    </li>
  );
}

function AddTask({
  goal,
  sectionId,
  placeholder,
}: {
  goal: GoalDetail;
  sectionId?: string;
  placeholder: string;
}) {
  const [text, setText] = useState('');
  const create = useCreateTask(() => setText(''));
  useErrorToast(create.error);
  const submit = (e: FormEvent) => {
    e.preventDefault();
    if (!text.trim()) return;
    create.mutate({
      title: text.trim().slice(0, 200),
      area: goal.area,
      goal_id: goal.id,
      ...(sectionId ? { goal_section_id: sectionId } : {}),
    });
  };
  return (
    <form onSubmit={submit} className="flex items-center gap-2 pt-1">
      <span className="text-brand-500" aria-hidden="true">
        +
      </span>
      <input
        value={text}
        onChange={(e) => setText(e.target.value)}
        placeholder={placeholder}
        aria-label={placeholder}
        className="min-w-0 flex-1 bg-transparent py-1 text-theme-sm outline-none placeholder:k-muted"
      />
    </form>
  );
}

export function StepsPart({ goal, today }: { goal: GoalDetail; today: string }) {
  const [name, setName] = useState('');
  const add = useAddSection(goal.id, () => setName(''));
  useErrorToast(add.error);
  const block = (key: string, title: string, tasks: Task[], sectionId?: string) => {
    const k = tasks.filter((t) => t.status === 'done').length;
    return (
      <div
        key={key}
        className="grid gap-1 rounded-lg border border-gray-200 px-3 py-2.5 dark:border-white/10"
      >
        <div className="grid grid-cols-[minmax(0,1fr)_88px_auto] items-center gap-3">
          <span className="truncate text-theme-sm font-medium" data-num-ok>
            {title}
          </span>
          <span className="h-1.5 overflow-hidden rounded-full bg-gray-100 dark:bg-white/10">
            <span
              className="block h-full rounded-full bg-brand-500"
              style={{ width: `${tasks.length ? (k / tasks.length) * 100 : 0}%` }}
            />
          </span>
          <span className="text-theme-xs k-muted num">
            {k}/{tasks.length}
          </span>
        </div>
        <ul>
          {tasks.map((t) => (
            <GoalTask key={t.id} task={t} today={today} />
          ))}
        </ul>
        <AddTask goal={goal} sectionId={sectionId} placeholder="A task in this section" />
      </div>
    );
  };
  return (
    <div className="grid gap-2.5">
      {goal.sections.map((s) => block(s.id, s.name, s.tasks, s.id))}
      {(goal.tasks.length > 0 || goal.sections.length === 0) &&
        block('loose', goal.sections.length ? 'Other' : 'Tasks', goal.tasks)}
      <form
        onSubmit={(e) => {
          e.preventDefault();
          if (name.trim()) add.mutate(name.trim());
        }}
        className="flex items-center gap-2"
      >
        <input
          value={name}
          onChange={(e) => setName(e.target.value)}
          placeholder="+ Section"
          aria-label="New section"
          className="min-w-0 flex-1 bg-transparent py-1 text-theme-sm k-muted outline-none placeholder:k-muted"
        />
      </form>
    </div>
  );
}

export function CountPart({ goal, today }: { goal: GoalDetail; today: string }) {
  const tasks = goal.tasks.concat(goal.sections.flatMap((s) => s.tasks));
  const done = tasks.filter((t) => t.status === 'done').length;
  const target = goal.target_count ?? 1;
  return (
    <div className="grid gap-3">
      <div
        className="grid gap-1"
        style={{ gridTemplateColumns: `repeat(${Math.min(target, 24)}, minmax(0, 1fr))` }}
        aria-label={`${done} of ${target}`}
      >
        {shelf(Math.min(target, 24), done).map((c, i) => (
          <span
            key={i}
            className={cn(
              'h-6 rounded',
              c === 'done'
                ? 'bg-brand-500'
                : c === 'next'
                  ? 'border-[1.5px] border-dashed border-brand-500 bg-brand-50 dark:bg-brand-500/15'
                  : 'bg-gray-100 dark:bg-white/10',
            )}
          />
        ))}
      </div>
      <div className="rounded-lg border border-gray-200 px-3 py-2 dark:border-white/10">
        <ul>
          {tasks.map((t) => (
            <GoalTask key={t.id} task={t} today={today} />
          ))}
        </ul>
        <AddTask goal={goal} placeholder="Next one" />
      </div>
    </div>
  );
}

export function BinaryPart({ goal, today }: { goal: GoalDetail; today: string }) {
  const save = useSaveGoal(goal.id);
  useErrorToast(save.error);
  const [confirm, setConfirm] = useState<'pass' | 'fail' | null>(null);
  const [label, setLabel] = useState('');
  const [mdate, setMdate] = useState('');
  const addMilestone = useAddMilestone(goal.id, () => {
    setLabel('');
    setMdate('');
  });
  useErrorToast(addMilestone.error);
  const total = Math.max(1, daysBetween(goal.start_date, goal.due));
  return (
    <div className="grid gap-3">
      <Meter
        label={
          <>
            {shortDate(goal.start_date)} – {shortDate(goal.due)} ·{' '}
            <Num>{goal.progress.days_left} d</Num> left
          </>
        }
        value={Math.max(0, daysBetween(goal.start_date, today))}
        max={total}
      />
      {goal.milestones.length > 0 && (
        <ol className="k-milestones">
          {[...goal.milestones]
            .sort((x, y) => (x.date < y.date ? -1 : 1))
            .map((m) => (
              <li key={m.id} className={m.date <= today ? 'k-ms-past' : undefined}>
                <span className="k-muted">{shortDate(m.date)}</span> {m.label}
              </li>
            ))}
        </ol>
      )}
      <form
        onSubmit={(e) => {
          e.preventDefault();
          if (label.trim() && mdate) addMilestone.mutate({ date: mdate, label: label.trim() });
        }}
        className="k-inline-form"
      >
        <input
          value={label}
          onChange={(e) => setLabel(e.target.value)}
          placeholder="Milestone"
          aria-label="Milestone"
          className={compactInput}
        />
        <input
          type="date"
          value={mdate}
          onChange={(e) => setMdate(e.target.value)}
          aria-label="Milestone date"
          className={compactInput}
        />
        <button type="submit" disabled={addMilestone.isPending} className={ghost}>
          Add
        </button>
      </form>
      {goal.result ? (
        <span className={goal.result === 'pass' ? 'k-pos' : 'k-error-text'}>
          {goal.result === 'pass' ? 'Passed' : 'Not passed'}
        </span>
      ) : (
        <div className="flex flex-wrap items-center gap-3">
          <span className="rounded-lg bg-brand-50 px-3 py-1.5 text-theme-sm font-medium text-brand-500 dark:bg-brand-500/15">
            Not decided yet
          </span>
          <button
            type="button"
            disabled={save.isPending}
            onClick={() => setConfirm('pass')}
            className={ghost}
          >
            Passed
          </button>
          <button
            type="button"
            disabled={save.isPending}
            onClick={() => setConfirm('fail')}
            className="text-theme-sm font-medium k-error-text disabled:opacity-50"
          >
            Not passed
          </button>
        </div>
      )}
      {confirm && (
        <ConfirmDialog
          title={confirm === 'pass' ? 'Mark as passed?' : 'Mark as not passed?'}
          description="The result closes the goal and is not taken back."
          confirmLabel={confirm === 'pass' ? 'Passed' : 'Not passed'}
          destructive={confirm === 'fail'}
          busy={save.isPending}
          onConfirm={() => {
            save.mutate({ result: confirm });
            setConfirm(null);
          }}
          onClose={() => setConfirm(null)}
        />
      )}
    </div>
  );
}

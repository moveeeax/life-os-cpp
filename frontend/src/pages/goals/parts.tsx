import { useState, type FormEvent } from 'react';

import { useAddMilestone, useAddSection, usePutCheckin, useSaveGoal } from '@/hooks/useGoals';
import { useCreateTask, useUpdateTask } from '@/hooks/useTasks';
import { formatValue, shelf, type GoalDetail } from '@/lib/goals';
import { dueLabel, type Task } from '@/lib/tasks';
import { cn } from '@/lib/utils';

const day = (s: string) => Date.parse(`${s}T12:00:00Z`);
const shortDate = (s: string) =>
  new Date(day(s)).toLocaleDateString('en-GB', { day: 'numeric', month: 'short', timeZone: 'UTC' });
const inputClass =
  'h-9 rounded-lg border border-gray-200 bg-white px-3 text-theme-sm dark:border-white/10 dark:bg-white/5';
const ghost = 'text-theme-sm font-medium text-brand-500 disabled:opacity-50';
const STATUS: Record<Task['status'], string> = {
  done: 'done',
  in_progress: 'in progress',
  open: 'not started',
};

/** Check-ins against the even-pace line, drawn to scale. */
function NumberChart({ goal, today }: { goal: GoalDetail; today: string }) {
  const W = 640;
  const H = 200;
  const P = { l: 46, r: 12, t: 12, b: 26 };
  const points: [string, number][] = [
    [goal.start_date, goal.start_value ?? 0],
    ...goal.checkins
      .slice()
      .reverse()
      .filter((c) => c.date !== goal.start_date)
      .map((c): [string, number] => [c.date, c.value]),
  ];
  const from = goal.start_value ?? 0;
  const to = goal.target_value ?? 0;
  const values = points.map((p) => p[1]).concat([from, to]);
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
  return (
    <svg
      viewBox={`0 0 ${W} ${H}`}
      className="h-auto w-full"
      role="img"
      aria-label="Check-ins against the even pace"
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
          <text x={P.l - 6} y={Y(v) + 4} textAnchor="end" fontSize="11" className="fill-gray-400">
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
      {day(today) >= t0 && day(today) <= t1 && (
        <>
          <line
            x1={X(today)}
            x2={X(today)}
            y1={P.t}
            y2={H - P.b}
            className="stroke-gray-200 dark:stroke-white/10"
          />
          <text x={X(today)} y={H - 8} textAnchor="middle" fontSize="11" className="fill-gray-400">
            today
          </text>
        </>
      )}
      <text x={P.l} y={H - 8} fontSize="11" className="fill-gray-400">
        {shortDate(goal.start_date)}
      </text>
      <text x={W - P.r} y={H - 8} textAnchor="end" fontSize="11" className="fill-gray-400">
        {shortDate(goal.due)}
      </text>
      <polyline
        points={points.map(([d, v]) => `${X(d)},${Y(v)}`).join(' ')}
        fill="none"
        strokeWidth="2"
        className="stroke-brand-500"
      />
      {points.map(([d, v]) => (
        <circle key={d} cx={X(d)} cy={Y(v)} r="3" className="fill-brand-500" />
      ))}
      <circle
        cx={X(last[0])}
        cy={Y(last[1])}
        r="5"
        strokeWidth="2"
        className="fill-white stroke-brand-500 dark:fill-gray-900"
      />
    </svg>
  );
}

export function NumberPart({ goal, today }: { goal: GoalDetail; today: string }) {
  const [value, setValue] = useState('');
  const [date, setDate] = useState(today);
  const put = usePutCheckin(goal.id, () => setValue(''));
  const v = Number(value.replace(',', '.'));
  const submit = (e: FormEvent) => {
    e.preventDefault();
    if (value.trim() && Number.isFinite(v) && date) put.mutate({ value: v, date });
  };
  return (
    <div className="grid gap-3">
      <NumberChart goal={goal} today={today} />
      <form onSubmit={submit} className="flex flex-wrap items-center gap-2">
        <input
          value={value}
          onChange={(e) => setValue(e.target.value)}
          inputMode="decimal"
          placeholder={goal.unit || 'value'}
          aria-label="Value"
          className={cn(inputClass, 'w-28')}
        />
        <input
          type="date"
          value={date}
          onChange={(e) => setDate(e.target.value)}
          aria-label="Date"
          className={inputClass}
        />
        <button
          type="submit"
          disabled={put.isPending}
          className="h-9 rounded-lg bg-brand-500 px-4 text-theme-sm font-medium text-white disabled:opacity-50"
        >
          Check in
        </button>
      </form>
      {put.error && (
        <p role="alert" className="text-theme-sm text-error-500">
          {put.error}
        </p>
      )}
      {goal.checkins.length > 0 && (
        <p className="text-theme-xs text-gray-400 tabular-nums">
          {goal.checkins
            .slice(0, 4)
            .map((c) => `${shortDate(c.date)} · ${formatValue(c.value, goal.unit)}`)
            .join('  ·  ')}
        </p>
      )}
    </div>
  );
}

/** A task line inside a goal: round check, title, status or done word. */
function GoalTask({ task, today }: { task: Task; today: string }) {
  const update = useUpdateTask(task.id);
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
      <span
        className={cn(
          'truncate',
          done ? 'text-gray-400 line-through' : 'text-gray-800 dark:text-white/90',
        )}
      >
        {task.title}
      </span>
      <span
        className={cn(
          'text-theme-xs whitespace-nowrap',
          task.status === 'in_progress' ? 'text-brand-500' : 'text-gray-400',
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
        className="min-w-0 flex-1 bg-transparent py-1 text-theme-sm outline-none placeholder:text-gray-400"
      />
    </form>
  );
}

export function StepsPart({ goal, today }: { goal: GoalDetail; today: string }) {
  const [name, setName] = useState('');
  const add = useAddSection(goal.id, () => setName(''));
  const block = (key: string, title: string, tasks: Task[], sectionId?: string) => {
    const k = tasks.filter((t) => t.status === 'done').length;
    return (
      <div
        key={key}
        className="grid gap-1 rounded-lg border border-gray-200 px-3 py-2.5 dark:border-white/10"
      >
        <div className="grid grid-cols-[minmax(0,1fr)_88px_auto] items-center gap-3">
          <span className="truncate text-theme-sm font-medium text-gray-800 dark:text-white/90">
            {title}
          </span>
          <span className="h-1.5 overflow-hidden rounded-full bg-gray-100 dark:bg-white/10">
            <span
              className="block h-full rounded-full bg-brand-500"
              style={{ width: `${tasks.length ? (k / tasks.length) * 100 : 0}%` }}
            />
          </span>
          <span className="text-theme-xs text-gray-500 tabular-nums">
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
          className="min-w-0 flex-1 bg-transparent py-1 text-theme-sm text-gray-500 outline-none placeholder:text-gray-400"
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
  const [label, setLabel] = useState('');
  const [mdate, setMdate] = useState('');
  const addMilestone = useAddMilestone(goal.id, () => {
    setLabel('');
    setMdate('');
  });
  const total = Math.max(1, (day(goal.due) - day(goal.start_date)) / 86_400_000);
  const pos = (d: string) =>
    `${Math.min(100, Math.max(0, ((day(d) - day(goal.start_date)) / 86_400_000 / total) * 100))}%`;
  return (
    <div className="grid gap-3">
      <div
        className="relative h-14"
        aria-label={`From ${shortDate(goal.start_date)} to ${shortDate(goal.due)}, today marked`}
      >
        <span className="absolute inset-x-0 top-[22px] h-1.5 rounded-full bg-gray-100 dark:bg-white/10" />
        <span
          className="absolute left-0 top-[22px] h-1.5 rounded-full bg-brand-500"
          style={{ width: pos(today) }}
        />
        {goal.milestones.map((m) => (
          <span
            key={m.id}
            title={m.label}
            className="absolute top-[18px] size-3.5 -translate-x-1/2 rounded-full border-2 border-white bg-gray-400 dark:border-gray-900"
            style={{ left: pos(m.date) }}
          />
        ))}
        <span
          className="absolute top-3 h-6 w-0.5 bg-gray-800 dark:bg-white/90"
          style={{ left: pos(today) }}
        />
        <span
          className="absolute top-9 -translate-x-1/2 text-theme-xs whitespace-nowrap text-gray-500 tabular-nums"
          style={{ left: pos(today) }}
        >
          today · {goal.progress.days_left} d left
        </span>
      </div>
      {goal.milestones.length > 0 && (
        <p className="text-theme-xs text-gray-400">
          {goal.milestones.map((m) => `${shortDate(m.date)} · ${m.label}`).join('  ·  ')}
        </p>
      )}
      <form
        onSubmit={(e) => {
          e.preventDefault();
          if (label.trim() && mdate) addMilestone.mutate({ date: mdate, label: label.trim() });
        }}
        className="flex flex-wrap items-center gap-2"
      >
        <input
          value={label}
          onChange={(e) => setLabel(e.target.value)}
          placeholder="Milestone"
          aria-label="Milestone"
          className={cn(inputClass, 'w-40')}
        />
        <input
          type="date"
          value={mdate}
          onChange={(e) => setMdate(e.target.value)}
          aria-label="Milestone date"
          className={inputClass}
        />
        <button type="submit" disabled={addMilestone.isPending} className={ghost}>
          Add
        </button>
      </form>
      {goal.result ? (
        <span
          className={cn(
            'justify-self-start rounded-lg px-3 py-1.5 text-theme-sm font-semibold',
            goal.result === 'pass'
              ? 'bg-success-50 text-success-600 dark:bg-success-500/15'
              : 'bg-error-50 text-error-600 dark:bg-error-500/15',
          )}
        >
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
            onClick={() => save.mutate({ result: 'pass' })}
            className={ghost}
          >
            Passed
          </button>
          <button
            type="button"
            disabled={save.isPending}
            onClick={() => save.mutate({ result: 'fail' })}
            className="text-theme-sm font-medium text-error-500 disabled:opacity-50"
          >
            Not passed
          </button>
        </div>
      )}
    </div>
  );
}

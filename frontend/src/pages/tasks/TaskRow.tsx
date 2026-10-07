import { useState } from 'react';
import { Link } from 'react-router';

import { Modal } from '@/components/Modal';
import { useUpdateTask } from '@/hooks/useTasks';
import { areaOf, dueLabel, EFFORT_LABEL, safeLink, type Task } from '@/lib/tasks';
import { cn } from '@/lib/utils';

import { TaskForm } from './TaskForm';

const TONE = { late: 'text-error-500', today: 'text-warning-600', plain: 'text-gray-400' } as const;

/** One task: a round check, the area dot, the title, the due on the right; details open under it. */
export function TaskRow({
  task,
  today,
  open,
  onToggle,
}: {
  task: Task;
  today: string;
  open: boolean;
  onToggle: () => void;
}) {
  const update = useUpdateTask(task.id);
  const [editing, setEditing] = useState(false);
  const done = task.status === 'done';
  // A closed task's deadline no longer asks for anything.
  const due = done ? null : dueLabel(task.due, today);
  const area = areaOf(task.area);
  const link = task.source_kind === 'url' ? safeLink(task.source_ref) : null;
  return (
    <li>
      <div className="grid grid-cols-[20px_minmax(0,1fr)_auto] items-center gap-3.5 py-2.5">
        <button
          type="button"
          aria-label={done ? `Reopen: ${task.title}` : `Done: ${task.title}`}
          disabled={update.isPending}
          onClick={() => update.mutate({ status: done ? 'open' : 'done' })}
          className={cn(
            'size-[18px] rounded-full border-[1.5px]',
            done ? 'border-success-500 bg-success-500' : 'border-gray-400 hover:border-gray-600',
          )}
        />
        <button
          type="button"
          onClick={onToggle}
          aria-expanded={open}
          className="flex min-w-0 items-center gap-2.5 text-start"
        >
          <span className={cn('size-[7px] shrink-0 rounded-full', area.dot)} aria-hidden="true" />
          <span
            className={cn(
              'truncate text-theme-sm',
              done ? 'text-gray-400 line-through' : 'text-gray-800 dark:text-white/90',
            )}
          >
            {task.title}
          </span>
        </button>
        {due ? (
          <span className={cn('text-theme-xs whitespace-nowrap tabular-nums', TONE[due.tone])}>
            {due.text}
          </span>
        ) : (
          <span />
        )}
      </div>
      {open && (
        <div className="grid gap-1 ps-[34px] pb-3.5 text-theme-sm text-gray-500">
          <p>
            <span className="font-medium text-gray-800 dark:text-white/90">Next:</span>{' '}
            {task.next_step || 'not set'}
          </p>
          <p>
            {area.label}
            {task.effort && ` · ${EFFORT_LABEL[task.effort]}`}
            {link && (
              <>
                {' · '}
                <a
                  href={link}
                  target="_blank"
                  rel="noreferrer"
                  className="text-brand-500 hover:underline"
                >
                  source
                </a>
              </>
            )}
            {task.source_kind === 'money_transaction' && (
              <>
                {' · '}
                {/* The ledger has no link to one row yet: this opens the ledger. */}
                <Link to="/money" className="text-brand-500 hover:underline">
                  from the ledger
                </Link>
              </>
            )}
          </p>
          {task.note && <p className="whitespace-pre-line">{task.note}</p>}
          <div className="mt-1 flex gap-3.5">
            <button
              type="button"
              onClick={() => setEditing(true)}
              className="text-gray-500 hover:text-gray-800 dark:hover:text-gray-200"
            >
              Edit
            </button>
          </div>
          {update.error && (
            <p role="alert" className="text-error-500">
              {update.error}
            </p>
          )}
        </div>
      )}
      {editing && (
        <Modal onClose={() => setEditing(false)} className="max-w-lg">
          <TaskForm task={task} onClose={() => setEditing(false)} />
        </Modal>
      )}
    </li>
  );
}

/** Rows under one caption, one open at a time. */
export function TaskList({ tasks, today }: { tasks: Task[]; today: string }) {
  const [open, setOpen] = useState<string | null>(null);
  return (
    <ul className="divide-y divide-gray-100 dark:divide-white/5">
      {tasks.map((t) => (
        <TaskRow
          key={t.id}
          task={t}
          today={today}
          open={open === t.id}
          onToggle={() => setOpen(open === t.id ? null : t.id)}
        />
      ))}
    </ul>
  );
}

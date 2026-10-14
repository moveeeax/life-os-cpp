import { useState } from 'react';
import { Link } from 'react-router';

import { Modal } from '@/components/Modal';
import { useUpdateTask } from '@/hooks/useTasks';
import { useNotesEnabled } from '@/hooks/useNotes';
import { areaOf, dueLabel, EFFORT_LABEL, safeLink, type Task } from '@/lib/tasks';
import { cn } from '@/lib/utils';

import { NoteSheet } from '../notes/NoteSheet';
import { TaskForm } from './TaskForm';

const TONE = { late: 'k-error-text', today: 'k-warn-text', plain: 'k-muted' } as const;

/** One task: a round check, the area dot, the title, the due on the right; details open under it. */
export function TaskRow({
  task,
  today,
  open,
  onToggle,
  hideProject,
}: {
  task: Task;
  today: string;
  open: boolean;
  onToggle: () => void;
  /** On the project's own page its name would only repeat. */
  hideProject?: boolean;
}) {
  const update = useUpdateTask(task.id);
  const [editing, setEditing] = useState(false);
  const [moving, setMoving] = useState(false);
  const [noting, setNoting] = useState(false);
  const notesOn = useNotesEnabled(true);
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
            done ? 'border-success-500 bg-success-500' : 'k-check-ring',
          )}
        />
        <button
          type="button"
          onClick={onToggle}
          aria-expanded={open}
          className="flex min-w-0 items-center gap-2.5 text-start"
        >
          <span className={cn('size-[7px] shrink-0 rounded-full', area.dot)} aria-hidden="true" />
          <span className={cn('truncate text-theme-sm', done ? 'k-muted line-through' : '')}>
            <span data-num-ok>{task.title}</span>
            {task.project_title && !hideProject && (
              <span className="k-muted" data-num-ok>
                {' · '}
                {task.project_title}
              </span>
            )}
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
        <div className="grid gap-1 ps-[34px] pb-3.5 text-theme-sm k-muted">
          <p>
            <span className="font-medium">Next:</span> {task.next_step || 'not set'}
          </p>
          <p>
            {area.label}
            {task.effort && ` · ${EFFORT_LABEL[task.effort]}`}
            {link && (
              <>
                {' · '}
                <a href={link} target="_blank" rel="noreferrer" className="k-link">
                  source
                </a>
              </>
            )}
            {task.source_kind === 'money_transaction' && (
              <>
                {' · '}
                <Link
                  to={`/money?row=${encodeURIComponent(task.source_ref ?? '')}`}
                  className="k-link"
                >
                  from the ledger
                </Link>
              </>
            )}
          </p>
          {task.project_id && task.project_title && (
            <p>
              {task.project_kind === 'course' ? 'Course' : 'Project'}:{' '}
              <Link
                to={
                  task.project_kind === 'course'
                    ? `/learning/${task.project_id}`
                    : `/tasks/projects/${task.project_id}`
                }
                className="k-link"
                data-num-ok
              >
                {task.project_title}
              </Link>
            </p>
          )}
          {task.goal_title && (
            <p>
              Goal:{' '}
              <Link to="/goals" className="k-link">
                {task.goal_title}
              </Link>
              {task.status === 'in_progress' && ' · in progress'}
            </p>
          )}
          {task.note && <p className="whitespace-pre-line">{task.note}</p>}
          <div className="mt-1 flex gap-3.5">
            {!moving ? (
              <button
                type="button"
                onClick={() => setMoving(true)}
                className="k-muted hover:text-gray-800 dark:hover:text-gray-200"
              >
                Move date
              </button>
            ) : (
              <span className="flex items-center gap-2">
                <input
                  type="date"
                  autoFocus
                  defaultValue={task.due ?? ''}
                  aria-label="New due date"
                  onChange={(e) => {
                    update.mutate({ due: e.target.value || null });
                    setMoving(false);
                  }}
                  className="h-7 rounded-md border border-gray-200 bg-transparent px-2 text-theme-xs dark:border-white/10"
                />
                {task.due && (
                  <button
                    type="button"
                    onClick={() => {
                      update.mutate({ due: null });
                      setMoving(false);
                    }}
                    className="text-theme-xs k-muted"
                  >
                    No date
                  </button>
                )}
              </span>
            )}
            <button
              type="button"
              onClick={() => setEditing(true)}
              className="k-muted hover:text-gray-800 dark:hover:text-gray-200"
            >
              Edit
            </button>
            {notesOn && (
              <button
                type="button"
                onClick={() => setNoting(true)}
                className="k-muted hover:text-gray-800 dark:hover:text-gray-200"
              >
                Make a note
              </button>
            )}
          </div>
          {update.error && (
            <p role="alert" className="k-error-text">
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
      {noting && (
        <NoteSheet
          title={task.title}
          taskId={task.id}
          projectId={task.project_id ?? null}
          onClose={() => setNoting(false)}
        />
      )}
    </li>
  );
}

/** Rows under one caption, one open at a time. */
export function TaskList({
  tasks,
  today,
  hideProject,
}: {
  tasks: Task[];
  today: string;
  hideProject?: boolean;
}) {
  const [open, setOpen] = useState<string | null>(null);
  return (
    <ul className="k-divide">
      {tasks.map((t) => (
        <TaskRow
          key={t.id}
          task={t}
          today={today}
          open={open === t.id}
          onToggle={() => setOpen(open === t.id ? null : t.id)}
          hideProject={hideProject}
        />
      ))}
    </ul>
  );
}

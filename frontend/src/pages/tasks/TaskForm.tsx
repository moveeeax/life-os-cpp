import { useState, type FormEvent } from 'react';

import { ConfirmDialog } from '@/components/ConfirmDialog';
import { useGoal, useGoals } from '@/hooks/useGoals';
import { useDeleteTask, useUpdateTask } from '@/hooks/useTasks';
import {
  AREAS,
  EFFORT_LABEL,
  localToday,
  type Area,
  type Effort,
  type Task,
  type TaskPatch,
} from '@/lib/tasks';

import {
  dangerButton,
  inputClass,
  labelClass,
  modalPanel,
  primaryButton,
  secondaryButton,
  textareaClass,
} from '../workout/styles';

const EFFORTS = Object.keys(EFFORT_LABEL) as Effort[];

/** Every field of a task; Save sends only what changed. */
export function TaskForm({ task, onClose }: { task: Task; onClose: () => void }) {
  const [title, setTitle] = useState(task.title);
  const [area, setArea] = useState<Area>(task.area);
  const [effort, setEffort] = useState<Effort | ''>(task.effort ?? '');
  const [due, setDue] = useState(task.due ?? '');
  const [next, setNext] = useState(task.next_step);
  const [note, setNote] = useState(task.note);
  const [status, setStatus] = useState<Task['status']>(task.status);
  const [goalId, setGoalId] = useState(task.goal_id ?? '');
  const [sectionId, setSectionId] = useState(task.goal_section_id ?? '');
  const today = localToday();
  const active = useGoals('active', today).data ?? [];
  // A task of a done or dropped goal still shows that goal, by its title.
  const goals =
    task.goal_id && !active.some((g) => g.id === task.goal_id)
      ? [{ id: task.goal_id, title: task.goal_title ?? 'This goal' }, ...active]
      : active;
  const goal = useGoal(goalId || null, today).data;
  const [confirm, setConfirm] = useState(false);
  const update = useUpdateTask(task.id, onClose);
  const remove = useDeleteTask(onClose);
  const ok = title.trim() !== '';

  const submit = (e: FormEvent) => {
    e.preventDefault();
    if (!ok) return;
    const patch: TaskPatch = {};
    if (title.trim() !== task.title) patch.title = title.trim();
    if (area !== task.area) patch.area = area;
    if ((effort || null) !== task.effort) patch.effort = effort || null;
    if ((due || null) !== task.due) patch.due = due || null;
    if (next.trim() !== task.next_step) patch.next_step = next.trim();
    if (note !== task.note) patch.note = note;
    if (status !== task.status) patch.status = status;
    if ((goalId || null) !== task.goal_id || (sectionId || null) !== task.goal_section_id) {
      // A section travels with its goal (the API checks the pair).
      patch.goal_id = goalId || null;
      patch.goal_section_id = goalId && sectionId ? sectionId : null;
    }
    if (Object.keys(patch).length === 0) {
      onClose();
      return;
    }
    update.mutate(patch);
  };

  return (
    <>
      <form onSubmit={submit} className={modalPanel} aria-labelledby="task-form-title">
        <h2 id="task-form-title" className="text-lg font-semibold">
          Edit task
        </h2>
        <div className="mt-4 grid grid-cols-1 gap-4 sm:grid-cols-2">
          <div className="sm:col-span-2">
            <label htmlFor="task-title" className={labelClass}>
              Title
            </label>
            <input
              id="task-title"
              value={title}
              onChange={(e) => setTitle(e.target.value)}
              maxLength={200}
              className={inputClass}
            />
          </div>
          <div>
            <label htmlFor="task-area" className={labelClass}>
              Area
            </label>
            <select
              id="task-area"
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
            <label htmlFor="task-effort" className={labelClass}>
              Effort
            </label>
            <select
              id="task-effort"
              value={effort}
              onChange={(e) => setEffort(e.target.value as Effort | '')}
              className={inputClass}
            >
              <option value="">Not set</option>
              {EFFORTS.map((k) => (
                <option key={k} value={k}>
                  {EFFORT_LABEL[k]}
                </option>
              ))}
            </select>
          </div>
          <div>
            <label htmlFor="task-due" className={labelClass}>
              Due (only a real deadline)
            </label>
            <input
              id="task-due"
              type="date"
              value={due}
              onChange={(e) => setDue(e.target.value)}
              className={inputClass}
            />
          </div>
          <div>
            <label htmlFor="task-next" className={labelClass}>
              Next step
            </label>
            <input
              id="task-next"
              value={next}
              onChange={(e) => setNext(e.target.value)}
              maxLength={300}
              className={inputClass}
            />
          </div>
          <div>
            <label htmlFor="task-status" className={labelClass}>
              Status
            </label>
            <select
              id="task-status"
              value={status}
              onChange={(e) => setStatus(e.target.value as Task['status'])}
              className={inputClass}
            >
              <option value="open">Not started</option>
              <option value="in_progress">In progress</option>
              <option value="done">Done</option>
            </select>
          </div>
          <div>
            <label htmlFor="task-goal" className={labelClass}>
              Goal
            </label>
            <select
              id="task-goal"
              value={goalId}
              onChange={(e) => {
                setGoalId(e.target.value);
                setSectionId('');
              }}
              className={inputClass}
            >
              <option value="">None</option>
              {goals.map((g) => (
                <option key={g.id} value={g.id}>
                  {g.title}
                </option>
              ))}
            </select>
          </div>
          {goal?.kind === 'steps' && goal.sections.length > 0 && (
            <div>
              <label htmlFor="task-section" className={labelClass}>
                Section
              </label>
              <select
                id="task-section"
                value={sectionId}
                onChange={(e) => setSectionId(e.target.value)}
                className={inputClass}
              >
                <option value="">None</option>
                {goal.sections.map((sec) => (
                  <option key={sec.id} value={sec.id}>
                    {sec.name}
                  </option>
                ))}
              </select>
            </div>
          )}
          <div className="sm:col-span-2">
            <label htmlFor="task-note" className={labelClass}>
              Note
            </label>
            <textarea
              id="task-note"
              rows={3}
              value={note}
              onChange={(e) => setNote(e.target.value)}
              maxLength={2000}
              className={textareaClass}
            />
          </div>
        </div>
        {(update.error || remove.error) && (
          <p role="alert" className="mt-3 text-theme-sm text-error-500">
            {update.error ?? remove.error}
          </p>
        )}
        <div className="mt-5 flex flex-wrap justify-between gap-3">
          <button type="button" onClick={() => setConfirm(true)} className={dangerButton}>
            Delete
          </button>
          <div className="flex gap-3">
            <button type="button" onClick={onClose} className={secondaryButton}>
              Cancel
            </button>
            <button type="submit" disabled={update.isPending || !ok} className={primaryButton}>
              {update.isPending ? 'Saving…' : 'Save'}
            </button>
          </div>
        </div>
      </form>
      {confirm && (
        <ConfirmDialog
          title="Delete this task?"
          description="It is gone for good; closing it keeps it in the history instead."
          confirmLabel="Delete"
          destructive
          busy={remove.isPending}
          onConfirm={() => {
            setConfirm(false);
            remove.mutate(task.id);
          }}
          onClose={() => setConfirm(false)}
        />
      )}
    </>
  );
}

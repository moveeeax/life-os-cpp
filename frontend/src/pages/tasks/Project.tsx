import { useState, type FormEvent } from 'react';
import { useNavigate, useParams } from 'react-router';

import { ConfirmDialog } from '@/components/ConfirmDialog';
import { useDeleteProject, useProject, useProjectTasks } from '@/hooks/useProjects';
import { useCreateTask } from '@/hooks/useTasks';
import { PROJECT_STATUS, safeLink } from '@/lib/tasks';
import { Group, Num } from '@/shell/kit';

import { useTasksDay } from './counts';
import { LoadError, Placeholder, TasksFrame } from './frame';
import { ProjectSheet } from './ProjectSheet';
import { TaskList } from './TaskRow';

/** One project: its open tasks, a quick add filed under it, the done ones folded. */
export function TasksProjectPage() {
  const { id = '' } = useParams();
  const navigate = useNavigate();
  const { counts, today } = useTasksDay();
  const project = useProject(id);
  const tasks = useProjectTasks(id);
  const [editing, setEditing] = useState(false);
  const [confirm, setConfirm] = useState(false);
  const [showDone, setShowDone] = useState(false);
  const [title, setTitle] = useState('');
  const create = useCreateTask(() => setTitle(''));
  const remove = useDeleteProject(() => navigate('/tasks/projects'));
  const p = project.data;
  const list = tasks.data ?? [];
  const open = list.filter((t) => t.status !== 'done');
  const done = list.filter((t) => t.status === 'done');
  const add = (e: FormEvent) => {
    e.preventDefault();
    if (title.trim()) create.mutate({ title: title.trim(), area: 'projects', project_id: id });
  };
  const link = p ? safeLink(p.link) : null;
  return (
    <TasksFrame
      counts={counts}
      headline={p ? <span data-num-ok>{p.name}</span> : 'Project'}
      sub={
        p && (
          <>
            {PROJECT_STATUS[p.status]} · <Num>{p.open}</Num> open
            {link && (
              <>
                {' · '}
                <a className="k-link" href={link} target="_blank" rel="noreferrer">
                  {new URL(link).host} ↗
                </a>
              </>
            )}
          </>
        )
      }
      tools={
        p && (
          <>
            <span className="k-grow" />
            <button type="button" className="k-btn" onClick={() => setEditing(true)}>
              Edit
            </button>
            <button type="button" className="k-btn k-danger" onClick={() => setConfirm(true)}>
              Delete
            </button>
          </>
        )
      }
    >
      {project.isPending ? (
        <Placeholder />
      ) : project.isError ? (
        <LoadError error={project.error} onRetry={() => void project.refetch()} />
      ) : (
        <>
          {p?.description && (
            <p className="k-sub" data-num-ok>
              {p.description}
            </p>
          )}
          <form className="k-capture-form" onSubmit={add}>
            <input
              className="k-input"
              aria-label="Add a task to this project"
              placeholder="Add a task to this project"
              maxLength={200}
              value={title}
              onChange={(e) => setTitle(e.target.value)}
            />
            <button type="submit" className="k-btn" disabled={create.isPending}>
              +
            </button>
          </form>
          {create.error && (
            <p role="alert" className="k-error">
              {create.error}
            </p>
          )}
          <Group label="Open" total={<Num>{open.length}</Num>}>
            {open.length ? (
              <TaskList tasks={open} today={today} hideProject />
            ) : (
              <p className="k-empty">Nothing open here.</p>
            )}
          </Group>
          {done.length > 0 && (
            <Group
              label="Done"
              total={
                <button type="button" className="k-link" onClick={() => setShowDone(!showDone)}>
                  {showDone ? 'Hide' : 'Show'} <Num>{done.length}</Num>
                </button>
              }
            >
              {showDone && <TaskList tasks={done} today={today} hideProject />}
            </Group>
          )}
        </>
      )}
      {editing && p && <ProjectSheet project={p} onClose={() => setEditing(false)} />}
      {confirm && p && (
        <ConfirmDialog
          title={`Delete ${p.name}?`}
          description="Its tasks stay in Tasks, without a project."
          confirmLabel="Delete"
          destructive
          busy={remove.isPending}
          onConfirm={() => remove.mutate(p.id)}
          onClose={() => setConfirm(false)}
        />
      )}
    </TasksFrame>
  );
}

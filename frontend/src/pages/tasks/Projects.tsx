import { useState } from 'react';
import { Link } from 'react-router';

import { useProjects } from '@/hooks/useProjects';
import type { Project } from '@/lib/tasks';
import { Group, Num, Row } from '@/shell/kit';

import { useTasksDay } from './counts';
import { LoadError, Placeholder, TasksFrame } from './frame';
import { ProjectSheet } from './ProjectSheet';

const DOT: Record<Project['status'], string> = {
  planned: 'var(--g-brand)',
  active: 'var(--g-ok)',
  paused: 'var(--g-ink-faint)',
  done: 'var(--g-ink-faint)',
};

function ProjectRow({ p }: { p: Project }) {
  return (
    <Row
      dot={DOT[p.status]}
      title={
        <Link to={`/tasks/projects/${p.id}`} className="k-row-link" data-num-ok>
          {p.name}
        </Link>
      }
      sub={
        <span data-num-ok>
          {p.next ? `next: ${p.next.title}` : p.status === 'paused' ? 'paused' : 'no open task'}
        </span>
      }
      right={
        <span>
          <Num>{p.open}</Num> open
        </span>
      }
    />
  );
}

/** The projects of Tasks: active, paused, done; courses live in Learning. */
export function TasksProjectsPage() {
  const { counts } = useTasksDay();
  const projects = useProjects('project');
  const [adding, setAdding] = useState(false);
  const [showDone, setShowDone] = useState(false);
  const list = projects.data ?? [];
  const active = list.filter((p) => p.status === 'active');
  const paused = list.filter((p) => p.status === 'paused');
  const done = list.filter((p) => p.status === 'done');
  const open = active.reduce((n, p) => n + p.open, 0);
  return (
    <TasksFrame
      counts={counts}
      headline={
        <>
          <Num>{active.length}</Num> active {active.length === 1 ? 'project' : 'projects'}
        </>
      }
      sub={
        <>
          <Num>{open}</Num> open tasks across them
        </>
      }
      tools={
        <>
          <span className="k-grow" />
          <button type="button" className="k-btn k-solid" onClick={() => setAdding(true)}>
            New project
          </button>
        </>
      }
    >
      {projects.isPending ? (
        <Placeholder />
      ) : projects.isError ? (
        <LoadError error={projects.error} onRetry={() => void projects.refetch()} />
      ) : list.length === 0 ? (
        <p className="k-empty">
          No projects yet. A project groups tasks; “#code” in a task phrase files it there.
        </p>
      ) : (
        <>
          {active.length > 0 && (
            <Group label="Active">
              {active.map((p) => (
                <ProjectRow key={p.id} p={p} />
              ))}
            </Group>
          )}
          {paused.length > 0 && (
            <Group label="Paused">
              {paused.map((p) => (
                <ProjectRow key={p.id} p={p} />
              ))}
            </Group>
          )}
          {done.length > 0 && (
            <Group
              label="Done"
              total={
                <button type="button" className="k-link" onClick={() => setShowDone(!showDone)}>
                  {showDone ? 'Hide' : 'Show'} <Num>{done.length}</Num>
                </button>
              }
            >
              {showDone && done.map((p) => <ProjectRow key={p.id} p={p} />)}
            </Group>
          )}
        </>
      )}
      {adding && <ProjectSheet onClose={() => setAdding(false)} />}
    </TasksFrame>
  );
}

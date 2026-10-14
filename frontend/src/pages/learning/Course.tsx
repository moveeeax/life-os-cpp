import { useState, type FormEvent } from 'react';
import { Link, useNavigate, useParams } from 'react-router';

import { ConfirmDialog } from '@/components/ConfirmDialog';
import { useNoteList, useNotesEnabled } from '@/hooks/useNotes';
import { useDeleteProject, useProject, useProjectTasks } from '@/hooks/useProjects';
import { useCreateTask } from '@/hooks/useTasks';
import { PROJECT_STATUS, courseProgress, localToday, safeLink } from '@/lib/tasks';
import { Group, LoadError, Meter, Num, Placeholder, Row, SectionFrame } from '@/shell/kit';

import { NoteSheet } from '../notes/NoteSheet';
import { ProjectSheet } from '../tasks/ProjectSheet';
import { TaskList } from '../tasks/TaskRow';

const short = (iso: string | null) =>
  iso
    ? new Date(`${iso}T00:00:00Z`).toLocaleDateString('en-US', {
        month: 'short',
        day: 'numeric',
        timeZone: 'UTC',
      })
    : '';

/** One course: its steps (tasks), its study notes, a step added here is filed under it. */
export function LearningCoursePage() {
  const { id = '' } = useParams();
  const navigate = useNavigate();
  const today = localToday();
  const course = useProject(id);
  const steps = useProjectTasks(id);
  const notesOn = useNotesEnabled(true);
  const notes = useNoteList({ project: id }, notesOn);
  const [editing, setEditing] = useState(false);
  const [confirm, setConfirm] = useState(false);
  const [noting, setNoting] = useState(false);
  const [title, setTitle] = useState('');
  const create = useCreateTask(() => setTitle(''));
  const remove = useDeleteProject(() => navigate('/learning'));
  const c = course.data;
  const progress = c ? courseProgress(c) : null;
  const list = steps.data ?? [];
  const open = list.filter((t) => t.status !== 'done');
  const done = list.filter((t) => t.status === 'done');
  const link = c ? safeLink(c.link) : null;
  const cert = c ? safeLink(c.certificate) : null;
  const add = (e: FormEvent) => {
    e.preventDefault();
    if (title.trim()) create.mutate({ title: title.trim(), area: 'growth', project_id: id });
  };
  return (
    <SectionFrame
      eyebrow="Learning · Course"
      headline={c ? <span data-num-ok>{c.name}</span> : 'Course'}
      sub={
        c && (
          <>
            <span data-num-ok>
              {[
                PROJECT_STATUS[c.status],
                c.platform,
                c.starts_on || c.ends_on ? `${short(c.starts_on)} – ${short(c.ends_on)}` : '',
              ]
                .filter(Boolean)
                .join(' · ')}
            </span>
            {link && (
              <>
                {' · '}
                <a className="k-link" href={link} target="_blank" rel="noreferrer">
                  course ↗
                </a>
              </>
            )}
            {cert && (
              <>
                {' · '}
                <a className="k-link" href={cert} target="_blank" rel="noreferrer">
                  certificate ↗
                </a>
              </>
            )}
          </>
        )
      }
      tools={
        c && (
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
      {course.isPending ? (
        <Placeholder />
      ) : course.isError ? (
        <LoadError
          error={course.error}
          onRetry={() => void course.refetch()}
          module="tasks"
          notFound="This course is not here: it may have been deleted."
        />
      ) : (
        <>
          {progress && <Meter value={progress.done} max={progress.total} />}
          <form className="k-capture-form" onSubmit={add}>
            <input
              className="k-input"
              aria-label="Add a step to this course"
              placeholder="Add a step, e.g. module 8: hybrid networking"
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
          <Group
            label="Steps"
            total={
              progress ? (
                <span>
                  <Num>{progress.done}</Num> of <Num>{progress.total}</Num>
                </span>
              ) : undefined
            }
          >
            {list.length === 0 ? (
              <p className="k-empty">No steps yet: add the first module, lab or exam.</p>
            ) : (
              <TaskList tasks={[...open, ...done]} today={today} hideProject />
            )}
          </Group>
          {notesOn && (
            <Group
              label="Notes"
              total={
                <button type="button" className="k-link" onClick={() => setNoting(true)}>
                  New note
                </button>
              }
            >
              {(notes.data?.data ?? []).length === 0 ? (
                <p className="k-empty">No study notes yet.</p>
              ) : (
                notes.data!.data.map((n) => (
                  <Row
                    key={n.id}
                    dot="var(--g-ok)"
                    title={
                      <Link to={`/notes/${n.id}`} className="k-row-link" data-num-ok>
                        {n.title}
                      </Link>
                    }
                    sub={
                      <span data-num-ok>
                        {[n.topic, n.tags.join(', ')].filter(Boolean).join(' · ')}
                      </span>
                    }
                  />
                ))
              )}
            </Group>
          )}
        </>
      )}
      {editing && c && <ProjectSheet project={c} onClose={() => setEditing(false)} />}
      {noting && <NoteSheet projectId={id} onClose={() => setNoting(false)} />}
      {confirm && c && (
        <ConfirmDialog
          title={`Delete ${c.name}?`}
          description="Its steps stay in Tasks and its notes in Notes, without the course."
          confirmLabel="Delete"
          destructive
          busy={remove.isPending}
          onConfirm={() => remove.mutate(c.id)}
          onClose={() => setConfirm(false)}
        />
      )}
    </SectionFrame>
  );
}

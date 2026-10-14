import { useState } from 'react';
import { Link } from 'react-router';

import { useProjects } from '@/hooks/useProjects';
import { courseProgress, type Project } from '@/lib/tasks';
import { Group, LoadError, Meter, Num, Placeholder, Row, SectionFrame } from '@/shell/kit';

import { ProjectSheet } from '../tasks/ProjectSheet';

const short = (iso: string | null) =>
  iso
    ? new Date(`${iso}T00:00:00Z`).toLocaleDateString('en-US', {
        month: 'short',
        day: 'numeric',
        timeZone: 'UTC',
      })
    : '';

function CourseRow({ c }: { c: Project }) {
  const p = courseProgress(c);
  const when =
    c.status === 'done'
      ? c.ends_on
        ? `done ${short(c.ends_on)}`
        : 'done'
      : c.ends_on
        ? `until ${short(c.ends_on)}`
        : '';
  return (
    <Row
      dot={
        c.status === 'active'
          ? 'var(--g-ok)'
          : c.status === 'planned'
            ? 'var(--g-brand)'
            : 'var(--g-ink-faint)'
      }
      title={
        <Link to={`/learning/${c.id}`} className="k-row-link" data-num-ok>
          {c.name}
        </Link>
      }
      sub={
        <span>
          <span data-num-ok>{[c.platform, when].filter(Boolean).join(' · ')}</span>
          {p && c.status === 'active' && <Meter value={p.done} max={p.total} />}
        </span>
      }
      right={
        p ? (
          <span>
            <Num>{p.done}</Num> of <Num>{p.total}</Num>
          </span>
        ) : (
          <span className="k-muted">no steps yet</span>
        )
      }
    />
  );
}

/** The courses: in progress, planned, done; a course is a project of kind course. */
export function LearningPage() {
  const courses = useProjects('course');
  const [adding, setAdding] = useState(false);
  const list = courses.data ?? [];
  const active = list.filter((c) => c.status === 'active');
  const planned = list.filter((c) => c.status === 'planned');
  const done = list.filter((c) => c.status === 'done');
  const lead = active[0];
  const leadProgress = lead ? courseProgress(lead) : null;
  return (
    <SectionFrame
      eyebrow="Learning"
      headline={
        lead && leadProgress ? (
          <>
            <span data-num-ok>{lead.name}</span>: <Num>{leadProgress.done}</Num> of{' '}
            <Num>{leadProgress.total}</Num>
          </>
        ) : lead ? (
          <span data-num-ok>{lead.name}</span>
        ) : (
          'Learning'
        )
      }
      sub={
        lead
          ? lead.ends_on
            ? `the course in progress · ends ${short(lead.ends_on)}`
            : 'the course in progress'
          : 'courses, their steps and notes'
      }
      tools={
        <>
          <span className="k-grow" />
          <button type="button" className="k-btn k-solid" onClick={() => setAdding(true)}>
            New course
          </button>
        </>
      }
    >
      {courses.isPending ? (
        <Placeholder />
      ) : courses.isError ? (
        <LoadError error={courses.error} onRetry={() => void courses.refetch()} module="tasks" />
      ) : list.length === 0 ? (
        <p className="k-empty">
          No courses yet. A course keeps its steps in Tasks and its study notes in Notes.
        </p>
      ) : (
        <>
          {active.length > 0 && (
            <Group label="In progress">
              {active.map((c) => (
                <CourseRow key={c.id} c={c} />
              ))}
            </Group>
          )}
          {planned.length > 0 && (
            <Group label="Planned">
              {planned.map((c) => (
                <CourseRow key={c.id} c={c} />
              ))}
            </Group>
          )}
          {done.length > 0 && (
            <Group label="Done">
              {done.map((c) => (
                <CourseRow key={c.id} c={c} />
              ))}
            </Group>
          )}
        </>
      )}
      {adding && <ProjectSheet kind="course" onClose={() => setAdding(false)} />}
    </SectionFrame>
  );
}

import { Link } from 'react-router';

import { Num } from '@/shell/kit';

import { Capture } from './Capture';
import { useTasksDay } from './counts';
import { LoadError, Placeholder, Section, TasksFrame } from './frame';
import { TaskList } from './TaskRow';

/** Late, the next two days, what was closed today, and one review line. */
export function TasksTodayPage() {
  const { today, agenda, counts } = useTasksDay();
  const a = agenda.data;
  const review = a ? a.review.no_next_step.length + a.review.stale.length : 0;
  return (
    <TasksFrame counts={counts}>
      <Capture />
      {agenda.isError ? (
        <LoadError error={agenda.error} onRetry={() => void agenda.refetch()} />
      ) : !a ? (
        <Placeholder className="h-40" />
      ) : (
        <>
          {a.late.length === 0 && a.soon.length === 0 && (
            <p className="text-theme-sm k-muted">Nothing due. Later has the rest.</p>
          )}
          {a.late.length > 0 && (
            <Section title="Late" tone="late">
              <TaskList tasks={a.late} today={today} />
            </Section>
          )}
          {a.soon.length > 0 && (
            <Section title="Today and the next two days">
              <TaskList tasks={a.soon} today={today} />
            </Section>
          )}
          {a.done_today.length > 0 && (
            <Section title="Done">
              <TaskList tasks={a.done_today} today={today} />
            </Section>
          )}
          {review > 0 && (
            <div className="k-banner">
              <span>
                Weekly review: <Num>{a.review.no_next_step.length}</Num> without a next step,{' '}
                <Num>{a.review.stale.length}</Num> untouched for two weeks
              </span>
              <Link to="/tasks/later#review">Look</Link>
            </div>
          )}
        </>
      )}
    </TasksFrame>
  );
}

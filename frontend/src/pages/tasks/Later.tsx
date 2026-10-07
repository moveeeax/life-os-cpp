import { useTasksDay } from './counts';
import { LoadError, Placeholder, Section, TasksFrame } from './frame';
import { TaskList } from './TaskRow';

/** Dated tasks by date, someday by creation, and the review lists. */
export function TasksLaterPage() {
  const { today, agenda, counts } = useTasksDay();
  const a = agenda.data;
  return (
    <TasksFrame counts={counts}>
      {agenda.isError ? (
        <LoadError error={agenda.error} onRetry={() => void agenda.refetch()} />
      ) : !a ? (
        <Placeholder className="h-40" />
      ) : (
        <>
          {a.dated.length === 0 && a.someday.length === 0 && (
            <p className="text-theme-sm text-gray-400">Nothing later. Today has the rest.</p>
          )}
          {a.dated.length > 0 && (
            <Section title="With a date">
              <TaskList tasks={a.dated} today={today} />
            </Section>
          )}
          {a.someday.length > 0 && (
            <Section title="Someday">
              <TaskList tasks={a.someday} today={today} />
            </Section>
          )}
          <div id="review" className="grid gap-7">
            {a.review.no_next_step.length > 0 && (
              <Section title="Without a next step">
                <TaskList tasks={a.review.no_next_step} today={today} />
              </Section>
            )}
            {a.review.stale.length > 0 && (
              <Section title="Untouched for two weeks">
                <TaskList tasks={a.review.stale} today={today} />
              </Section>
            )}
          </div>
        </>
      )}
    </TasksFrame>
  );
}

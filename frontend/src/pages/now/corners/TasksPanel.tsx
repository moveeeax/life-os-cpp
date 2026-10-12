import { Link } from 'react-router';

import { useUpdateTask } from '@/hooks/useTasks';
import type { Today } from '@/lib/now';
import { Capture } from '@/pages/tasks/Capture';

function TickRow({
  id,
  title,
  done,
  right,
  late,
}: {
  id: string;
  title: string;
  done: boolean;
  right: string;
  late?: boolean;
}) {
  const update = useUpdateTask(id);
  const pressed = update.isPending ? !done : done;
  return (
    <div className={pressed ? 'c-row c-done' : 'c-row'}>
      <button
        type="button"
        className="c-ring"
        aria-pressed={pressed}
        aria-label={`Done: ${title}`}
        disabled={update.isPending}
        onClick={() => update.mutate({ status: done ? 'open' : 'done' })}
      />
      <span className="c-t">{title}</span>
      <span className={late ? 'c-r c-late-text' : 'c-r'}>{right}</span>
    </div>
  );
}

function daysLate(due: string, today: string): string {
  const d = Math.round(
    (Date.parse(`${today}T00:00:00Z`) - Date.parse(`${due}T00:00:00Z`)) / 86_400_000,
  );
  return d === 1 ? '1 day late' : `${d} days late`;
}

export function TasksPanel({ today, date }: { today: Today; date: string }) {
  const t = today.tasks;
  const late = t?.late ?? [];
  return (
    <>
      <h2>
        Tasks <Link to="/tasks">Open Tasks →</Link>
      </h2>
      <div className="c-capture">
        <Capture defaultDue={date} />
      </div>
      {late.length > 0 && (
        <div>
          <span className="c-label c-late-text">Late</span>
          {late.map((x) => (
            <TickRow
              key={x.id}
              id={x.id}
              title={x.title}
              done={false}
              right={daysLate(x.due, date)}
              late
            />
          ))}
        </div>
      )}
      <div>
        <span className="c-label">Today</span>
        {(t?.today ?? []).length === 0 && <p className="c-empty">Nothing for today.</p>}
        {(t?.today ?? []).map((x) => (
          <TickRow
            key={x.id}
            id={x.id}
            title={x.title}
            done={x.status === 'done'}
            right={x.status === 'done' ? 'done' : 'today'}
          />
        ))}
      </div>
      {(t?.inbox_count ?? 0) > 0 && (
        <Link className="c-row c-plain c-link" to="/tasks/inbox">
          <span className="c-t">Inbox</span>
          <span className="c-r">
            <span className="num">{t!.inbox_count}</span> to sort
          </span>
        </Link>
      )}
    </>
  );
}

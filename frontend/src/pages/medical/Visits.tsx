import { useState } from 'react';

import { ConfirmDialog } from '@/components/ConfirmDialog';
import { useDateTask, useDeleteVisit, usePeople, useVisits } from '@/hooks/useMedical';
import { useErrorToast } from '@/hooks/useErrorToast';
import { formatDay, todayLocal } from '@/lib/dates';
import { upcomingFollowUps, type MedicalVisit } from '@/lib/medical';
import { Group, Row } from '@/shell/kit';

import { LoadError, MedicalFrame, NewButton, Placeholder } from './frame';
import { VisitSheet } from './Sheets';

/** The task button of a date: "Add to tasks" until there is one, then a quiet "In tasks". */
export function DateTaskButton({
  path,
  id,
  taskId,
}: {
  path: 'visits' | 'medications';
  id: string;
  taskId: string | null;
}) {
  const task = useDateTask(path);
  useErrorToast(task.error);
  if (taskId) return <span className="k-muted">In tasks</span>;
  return (
    <button
      type="button"
      className="k-link"
      disabled={task.isPending}
      onClick={() => task.mutate(id)}
    >
      {task.isPending ? 'Adding…' : 'Add to tasks'}
    </button>
  );
}

/** Visits to doctors, the newest first, with the follow-ups still ahead on top. */
export function MedicalVisitsPage() {
  const today = todayLocal();
  const visits = useVisits();
  const people = usePeople();
  const [editing, setEditing] = useState<MedicalVisit | 'new' | null>(null);
  const [deleting, setDeleting] = useState<MedicalVisit | null>(null);
  const remove = useDeleteVisit(() => setDeleting(null));
  useErrorToast(remove.error);
  const list = visits.data ?? [];
  const ahead = upcomingFollowUps(list, today);
  const noPeople = people.isSuccess && people.data.length === 0;
  const day = (d: string) => <span data-num-ok>{formatDay(d, { yearUnless: today })}</span>;

  const details = (v: MedicalVisit) => (
    <>
      {v.provider_name && <span>{v.provider_name}</span>}
      {v.outcome && <span className="k-pre num">{v.outcome}</span>}
      {v.follow_up && (
        <span className="k-actions">
          <span>Follow-up {day(v.follow_up)}</span>
          <DateTaskButton path="visits" id={v.id} taskId={v.follow_up_task_id} />
        </span>
      )}
      <span className="k-actions">
        <button type="button" className="k-link" onClick={() => setEditing(v)}>
          Edit
        </button>
        <button type="button" className="k-link k-error-text" onClick={() => setDeleting(v)}>
          Delete
        </button>
      </span>
    </>
  );

  return (
    <MedicalFrame
      headline={
        ahead.length === 0 ? (
          'Visits'
        ) : (
          <>
            {`${ahead[0].person_name}: follow-up `}
            {day(ahead[0].follow_up ?? '')}
          </>
        )
      }
      sub="who saw whom, what they said, when to come back"
      actions={!noPeople && <NewButton label="New visit" onClick={() => setEditing('new')} />}
    >
      {visits.isPending ? (
        <Placeholder />
      ) : visits.isError ? (
        <LoadError error={visits.error} onRetry={() => void visits.refetch()} />
      ) : noPeople ? (
        <p className="k-empty">Add the people of the family on Family first.</p>
      ) : list.length === 0 ? (
        <p className="k-empty">No visits yet.</p>
      ) : (
        <>
          {ahead.length > 0 && (
            <Group label="Coming up">
              {ahead.map((v) => (
                <Row
                  key={`ahead-${v.id}`}
                  dot="var(--g-warn)"
                  title={<span data-num-ok>{`${v.person_name}: ${v.reason}`}</span>}
                  sub={<>follow-up {day(v.follow_up ?? '')}</>}
                >
                  {details(v)}
                </Row>
              ))}
            </Group>
          )}
          <Group label="Visits">
            {list.map((v) => (
              <Row
                key={v.id}
                title={<span data-num-ok>{v.reason}</span>}
                sub={
                  <>
                    {`${v.person_name} · `}
                    {day(v.date)}
                  </>
                }
              >
                {details(v)}
              </Row>
            ))}
          </Group>
        </>
      )}
      {editing && (
        <VisitSheet
          visit={editing === 'new' ? undefined : editing}
          onClose={() => setEditing(null)}
        />
      )}
      {deleting && (
        <ConfirmDialog
          title={`Delete the visit “${deleting.reason}”?`}
          description="Records from it stay, without the link. This cannot be undone."
          confirmLabel="Delete"
          destructive
          busy={remove.isPending}
          onConfirm={() => remove.mutate(deleting.id)}
          onClose={() => setDeleting(null)}
        />
      )}
    </MedicalFrame>
  );
}

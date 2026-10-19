import { useState } from 'react';

import { ConfirmDialog } from '@/components/ConfirmDialog';
import { useDeleteMedication, useMedications, usePeople } from '@/hooks/useMedical';
import { useErrorToast } from '@/hooks/useErrorToast';
import { formatDay, todayLocal } from '@/lib/dates';
import { peopleLabel, splitMedications, type MedicalMedication } from '@/lib/medical';
import { Group, Row } from '@/shell/kit';

import { LoadError, MedicalFrame, NewButton, Placeholder } from './frame';
import { MedicationSheet } from './Sheets';
import { DateTaskButton } from './Visits';

/** Medications and supplements: what is taken now, then what was. Nothing here suggests a dose. */
export function MedicalMedicinesPage() {
  const today = todayLocal();
  const meds = useMedications();
  const people = usePeople();
  const [editing, setEditing] = useState<MedicalMedication | 'new' | null>(null);
  const [deleting, setDeleting] = useState<MedicalMedication | null>(null);
  const remove = useDeleteMedication(() => setDeleting(null));
  useErrorToast(remove.error);
  const { current, past } = splitMedications(meds.data ?? [], today);
  const noPeople = people.isSuccess && people.data.length === 0;
  const day = (d: string) => <span data-num-ok>{formatDay(d, { yearUnless: today })}</span>;

  const row = (m: MedicalMedication) => (
    <Row
      key={m.id}
      title={<span data-num-ok>{m.name}</span>}
      sub={
        <>
          {m.person_name}
          {m.end_date && (
            <>
              {m.end_date >= today ? ' · until ' : ' · ended '}
              {day(m.end_date)}
            </>
          )}
        </>
      }
      right={m.dose ? <span className="num">{m.dose}</span> : undefined}
    >
      {m.schedule && <span className="num">{m.schedule}</span>}
      {m.start_date && <span>Since {day(m.start_date)}</span>}
      {m.note && <span className="k-pre num">{m.note}</span>}
      {m.end_date && m.end_date >= today && (
        <span className="k-actions">
          <span>A task on the last day</span>
          <DateTaskButton path="medications" id={m.id} taskId={m.end_task_id} />
        </span>
      )}
      <span className="k-actions">
        <button type="button" className="k-link" onClick={() => setEditing(m)}>
          Edit
        </button>
        <button type="button" className="k-link k-error-text" onClick={() => setDeleting(m)}>
          Delete
        </button>
      </span>
    </Row>
  );

  return (
    <MedicalFrame
      headline={
        current.length === 0
          ? 'Medicines'
          : `Taken now by ${peopleLabel(current.map((m) => m.person_name))}`
      }
      sub="medications and supplements, as prescribed"
      actions={!noPeople && <NewButton label="New medication" onClick={() => setEditing('new')} />}
    >
      {meds.isPending ? (
        <Placeholder />
      ) : meds.isError ? (
        <LoadError error={meds.error} onRetry={() => void meds.refetch()} />
      ) : noPeople ? (
        <p className="k-empty">Add the people of the family on Family first.</p>
      ) : current.length + past.length === 0 ? (
        <p className="k-empty">No medications yet.</p>
      ) : (
        <>
          {current.length > 0 && <Group label="Now">{current.map(row)}</Group>}
          {past.length > 0 && <Group label="Finished">{past.map(row)}</Group>}
        </>
      )}
      {editing && (
        <MedicationSheet
          medication={editing === 'new' ? undefined : editing}
          onClose={() => setEditing(null)}
        />
      )}
      {deleting && (
        <ConfirmDialog
          title={`Delete ${deleting.name}?`}
          description="This cannot be undone."
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

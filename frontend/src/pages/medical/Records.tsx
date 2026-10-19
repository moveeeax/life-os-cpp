import { useState } from 'react';

import { Attachments } from '@/components/Attachments';
import { ConfirmDialog } from '@/components/ConfirmDialog';
import { useDeleteRecord, usePeople, useRecords } from '@/hooks/useMedical';
import { useErrorToast } from '@/hooks/useErrorToast';
import { formatDay, todayLocal } from '@/lib/dates';
import { RECORD_KIND, RECORD_KINDS, type MedicalRecord } from '@/lib/medical';
import { Filters, Group, Row } from '@/shell/kit';

import { LoadError, MedicalFrame, NewButton, Placeholder } from './frame';
import { RecordSheet } from './Sheets';

/** Lab results, scans, prescriptions of the family: a title and a date per row, the files inside. */
export function MedicalRecordsPage() {
  const today = todayLocal();
  const records = useRecords();
  const people = usePeople();
  const [person, setPerson] = useState('');
  const [kind, setKind] = useState('');
  const [editing, setEditing] = useState<MedicalRecord | 'new' | null>(null);
  const [openId, setOpenId] = useState<string | null>(null);
  const [deleting, setDeleting] = useState<MedicalRecord | null>(null);
  const remove = useDeleteRecord(() => setDeleting(null));
  useErrorToast(remove.error);

  const all = records.data ?? [];
  const list = all.filter((r) => (!person || r.person_id === person) && (!kind || r.kind === kind));
  const byYear = new Map<string, MedicalRecord[]>();
  for (const r of list) {
    const y = r.date.slice(0, 4);
    byYear.set(y, [...(byYear.get(y) ?? []), r]);
  }
  const noPeople = people.isSuccess && people.data.length === 0;

  return (
    <MedicalFrame
      headline="Records"
      sub="lab results, scans, prescriptions and certificates"
      actions={
        <>
          <Filters
            active={(person ? 1 : 0) + (kind ? 1 : 0)}
            onClear={() => {
              setPerson('');
              setKind('');
            }}
          >
            <label className="k-field">
              <span>Who</span>
              <select value={person} onChange={(e) => setPerson(e.target.value)}>
                <option value="">Everyone</option>
                {(people.data ?? []).map((p) => (
                  <option key={p.id} value={p.id}>
                    {p.name}
                  </option>
                ))}
              </select>
            </label>
            <label className="k-field">
              <span>Kind</span>
              <select value={kind} onChange={(e) => setKind(e.target.value)}>
                <option value="">Any</option>
                {RECORD_KINDS.map((k) => (
                  <option key={k} value={k}>
                    {RECORD_KIND[k]}
                  </option>
                ))}
              </select>
            </label>
          </Filters>
          {!noPeople && <NewButton label="New record" onClick={() => setEditing('new')} />}
        </>
      }
    >
      {records.isPending ? (
        <Placeholder />
      ) : records.isError ? (
        <LoadError error={records.error} onRetry={() => void records.refetch()} />
      ) : noPeople ? (
        <p className="k-empty">Add the people of the family on Family first.</p>
      ) : all.length === 0 ? (
        <p className="k-empty">No records yet: lab results, scans, discharge summaries.</p>
      ) : list.length === 0 ? (
        <p className="k-empty">Nothing matches the filter.</p>
      ) : (
        [...byYear.entries()].map(([year, rows]) => (
          <Group key={year} label={<span data-num-ok>{year}</span>}>
            {rows.map((r) => (
              <Row
                key={r.id}
                open={openId === r.id}
                onToggle={(o) => setOpenId(o ? r.id : null)}
                title={<span data-num-ok>{r.title}</span>}
                sub={
                  <>
                    {`${r.person_name} · ${RECORD_KIND[r.kind]} · `}
                    <span data-num-ok>{formatDay(r.date, { yearUnless: today })}</span>
                    {r.files > 0 && (
                      <>
                        {' · '}
                        <span data-num-ok>{r.files === 1 ? '1 file' : `${r.files} files`}</span>
                      </>
                    )}
                  </>
                }
              >
                {r.note && <span className="k-pre num">{r.note}</span>}
                <Attachments kind="medical_record" parentId={r.id} />
                <span className="k-actions">
                  <button type="button" className="k-link" onClick={() => setEditing(r)}>
                    Edit
                  </button>
                  <button
                    type="button"
                    className="k-link k-error-text"
                    onClick={() => setDeleting(r)}
                  >
                    Delete
                  </button>
                </span>
              </Row>
            ))}
          </Group>
        ))
      )}
      {editing && (
        <RecordSheet
          record={editing === 'new' ? undefined : editing}
          onClose={() => setEditing(null)}
          onCreated={(r) => setOpenId(r.id)}
        />
      )}
      {deleting && (
        <ConfirmDialog
          title={`Delete “${deleting.title}”?`}
          description="Its files are deleted from the storage too. This cannot be undone."
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

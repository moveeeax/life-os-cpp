import { useState } from 'react';

import { ConfirmDialog } from '@/components/ConfirmDialog';
import { useErrorToast } from '@/hooks/useErrorToast';
import { useDeleteMeasurement, useMeasurements, useSaveMeasurement } from '@/hooks/useMeasurements';
import { apiErrorMessage } from '@/lib/api/client';
import { formatDay } from '@/lib/dates';
import { today } from '@/lib/health';
import {
  GIRTHS,
  changeFrom,
  formatCm,
  girthsOf,
  measurementBody,
  measurementForm,
  measurementProblem,
  type MeasurementForm,
  type TapeMeasurement,
} from '@/lib/health/measurements';
import { Facts, Group, Num, Row, Sheet } from '@/shell/kit';

/** How many rows show before "Show all". */
const SHOWN = 5;

function MeasurementSheet({ m, onClose }: { m?: TapeMeasurement; onClose: () => void }) {
  const [f, setF] = useState<MeasurementForm>(() => measurementForm(today(), m));
  const [problem, setProblem] = useState<string | null>(null);
  const save = useSaveMeasurement(m?.id ?? null, onClose);
  const set = (k: keyof MeasurementForm) => (e: { target: { value: string } }) =>
    setF({ ...f, [k]: e.target.value });
  return (
    <Sheet title={m ? 'Edit the measurement' : 'New measurement'} onClose={onClose} width={460}>
      <form
        className="k-form"
        onSubmit={(e) => {
          e.preventDefault();
          const wrong = measurementProblem(f);
          setProblem(wrong);
          if (!wrong) save.mutate(measurementBody(f));
        }}
      >
        <label className="k-field">
          <span>Day</span>
          <input type="date" required max={today()} value={f.date} onChange={set('date')} />
        </label>
        <div className="k-fields k-fields-3">
          {GIRTHS.map(({ key, label }) => (
            <label key={key} className="k-field">
              <span>{label}, cm</span>
              <input inputMode="decimal" value={f[key]} onChange={set(key)} />
            </label>
          ))}
        </div>
        <label className="k-field">
          <span>Note (optional)</span>
          <input
            maxLength={500}
            placeholder="Morning, before breakfast"
            value={f.note}
            onChange={set('note')}
          />
        </label>
        <p className="k-sub">Any girth may stay empty; one is enough.</p>
        {(problem || save.error) && (
          <p role="alert" className="k-error">
            {problem ?? save.error}
          </p>
        )}
        <div className="k-dlg-actions">
          <button type="button" className="k-btn" onClick={onClose}>
            Cancel
          </button>
          <button type="submit" className="k-btn k-solid" disabled={save.isPending}>
            {save.isPending ? 'Saving…' : m ? 'Save' : 'Add'}
          </button>
        </div>
      </form>
    </Sheet>
  );
}

const signed = (d: number) => (d > 0 ? `+${d}` : String(d));

function MeasurementRow({
  m,
  before,
  onEdit,
  onDelete,
}: {
  m: TapeMeasurement;
  before?: TapeMeasurement;
  onEdit: () => void;
  onDelete: () => void;
}) {
  const girths = girthsOf(m);
  const change = changeFrom(m, before);
  const waist = m.waist_cm;
  return (
    <Row
      title={<span data-num-ok>{formatDay(m.date, { yearUnless: today() })}</span>}
      sub={girths.map((g) => g.label).join(', ')}
      right={waist != null ? <Num>{formatCm(waist)}</Num> : undefined}
    >
      <Facts
        items={GIRTHS.flatMap(({ key, label }) => {
          const v = m[key];
          if (v == null) return [];
          const d = change[key];
          return [
            {
              label,
              value: (
                <Num>
                  {formatCm(v)}
                  {d !== undefined && d !== 0 ? ` (${signed(d)})` : ''}
                </Num>
              ),
            },
          ];
        })}
      />
      {m.note && <span>{m.note}</span>}
      <span className="k-actions">
        <button type="button" className="k-link" onClick={onEdit}>
          Edit
        </button>
        <button type="button" className="k-link k-error-text" onClick={onDelete}>
          Delete
        </button>
      </span>
    </Row>
  );
}

/** The girths measured by hand, newest first; they need no Mi account. */
export function Measurements() {
  const q = useMeasurements();
  const [adding, setAdding] = useState(false);
  const [editing, setEditing] = useState<TapeMeasurement | null>(null);
  const [deleting, setDeleting] = useState<TapeMeasurement | null>(null);
  const [all, setAll] = useState(false);
  const remove = useDeleteMeasurement(() => setDeleting(null));
  useErrorToast(remove.error);
  const rows = q.data ?? [];
  const shown = all ? rows : rows.slice(0, SHOWN);
  return (
    <Group
      label="Measurements"
      total={
        <button type="button" className="k-link" onClick={() => setAdding(true)}>
          Add
        </button>
      }
    >
      {q.isPending ? (
        <div className="k-placeholder" />
      ) : q.isError ? (
        <p className="k-error">{apiErrorMessage(q.error, 'Failed to load.')}</p>
      ) : rows.length === 0 ? (
        <p className="k-empty">No measurements yet. Waist, chest, hips, neck, arm, thigh, in cm.</p>
      ) : (
        <>
          {shown.map((m, i) => (
            <MeasurementRow
              key={m.id}
              m={m}
              before={rows[i + 1]}
              onEdit={() => setEditing(m)}
              onDelete={() => setDeleting(m)}
            />
          ))}
          {rows.length > SHOWN && (
            <p className="k-sub">
              <button type="button" className="k-link" onClick={() => setAll((v) => !v)}>
                {all ? 'Show fewer' : 'Show all'}
              </button>
            </p>
          )}
        </>
      )}
      {adding && <MeasurementSheet onClose={() => setAdding(false)} />}
      {editing && <MeasurementSheet m={editing} onClose={() => setEditing(null)} />}
      {deleting && (
        <ConfirmDialog
          title={`Delete the measurement of ${formatDay(deleting.date, { yearUnless: today() })}?`}
          description="This cannot be undone."
          confirmLabel="Delete"
          destructive
          busy={remove.isPending}
          onConfirm={() => remove.mutate(deleting.id)}
          onClose={() => setDeleting(null)}
        />
      )}
    </Group>
  );
}

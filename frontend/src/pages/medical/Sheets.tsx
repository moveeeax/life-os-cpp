import { useState, type ReactNode } from 'react';

import {
  usePeople,
  useProviders,
  useSaveMedication,
  useSavePerson,
  useSaveProvider,
  useSaveRecord,
  useSaveVisit,
  useVisits,
} from '@/hooks/useMedical';
import {
  RECORD_KIND,
  RECORD_KINDS,
  orNull,
  type MedicalMedication,
  type MedicalPerson,
  type MedicalProvider,
  type MedicalRecord,
  type MedicalVisit,
} from '@/lib/medical';
import { todayLocal } from '@/lib/dates';
import { Sheet } from '@/shell/kit';

function Actions({
  busy,
  label,
  onCancel,
}: {
  busy: boolean;
  label: string;
  onCancel: () => void;
}) {
  return (
    <div className="k-dlg-actions">
      <button type="button" className="k-btn" onClick={onCancel}>
        Cancel
      </button>
      <button type="submit" className="k-btn k-solid" disabled={busy}>
        {busy ? 'Saving…' : label}
      </button>
    </div>
  );
}

function ErrorLine({ error }: { error: string | null }) {
  return error ? (
    <p role="alert" className="k-error">
      {error}
    </p>
  ) : null;
}

function Form({ onSubmit, children }: { onSubmit: () => void; children: ReactNode }) {
  return (
    <form
      className="k-form"
      onSubmit={(e) => {
        e.preventDefault();
        onSubmit();
      }}
    >
      {children}
    </form>
  );
}

type Change = { target: { value: string } };

/** The person select every visit, record and medication starts with. */
function PersonField({ value, onChange }: { value: string; onChange: (e: Change) => void }) {
  const people = usePeople();
  return (
    <label className="k-field">
      <span>Who</span>
      <select required value={value} onChange={onChange}>
        <option value="" disabled>
          {people.isPending ? 'Loading…' : 'Choose a person'}
        </option>
        {(people.data ?? []).map((p) => (
          <option key={p.id} value={p.id}>
            {p.name}
          </option>
        ))}
      </select>
    </label>
  );
}

/** The person every new row starts with: the only one, when there is one. */
function useFirstPerson(): string {
  const people = usePeople().data ?? [];
  return people.length === 1 ? people[0].id : '';
}

export function PersonSheet({ person, onClose }: { person?: MedicalPerson; onClose: () => void }) {
  const [f, setF] = useState({
    name: person?.name ?? '',
    relation: person?.relation ?? '',
    note: person?.note ?? '',
  });
  const save = useSavePerson(person?.id ?? null, onClose);
  const set = (k: keyof typeof f) => (e: Change) => setF({ ...f, [k]: e.target.value });
  return (
    <Sheet title={person ? 'Edit the person' : 'New person'} onClose={onClose} width={440}>
      <Form
        onSubmit={() =>
          save.mutate({ name: f.name.trim(), relation: f.relation.trim(), note: f.note.trim() })
        }
      >
        <div className="k-fields">
          <label className="k-field">
            <span>Name</span>
            <input required maxLength={80} value={f.name} onChange={set('name')} />
          </label>
          <label className="k-field">
            <span>Relation</span>
            <input
              maxLength={40}
              placeholder="me, wife, son"
              value={f.relation}
              onChange={set('relation')}
            />
          </label>
        </div>
        <label className="k-field">
          <span>Note</span>
          <textarea
            maxLength={1000}
            rows={3}
            placeholder="Blood type, allergies"
            value={f.note}
            onChange={set('note')}
          />
        </label>
        <ErrorLine error={save.error} />
        <Actions busy={save.isPending} label={person ? 'Save' : 'Add'} onCancel={onClose} />
      </Form>
    </Sheet>
  );
}

export function ProviderSheet({
  provider,
  onClose,
}: {
  provider?: MedicalProvider;
  onClose: () => void;
}) {
  const [f, setF] = useState({
    kind: provider?.kind ?? ('doctor' as MedicalProvider['kind']),
    name: provider?.name ?? '',
    specialty: provider?.specialty ?? '',
    clinic: provider?.clinic ?? '',
    phone: provider?.phone ?? '',
    email: provider?.email ?? '',
    address: provider?.address ?? '',
    note: provider?.note ?? '',
  });
  const save = useSaveProvider(provider?.id ?? null, onClose);
  const set = (k: keyof typeof f) => (e: Change) => setF({ ...f, [k]: e.target.value });
  const doctor = f.kind === 'doctor';
  return (
    <Sheet
      title={provider ? 'Edit' : doctor ? 'New doctor' : 'New clinic'}
      onClose={onClose}
      width={480}
    >
      <Form
        onSubmit={() =>
          save.mutate({
            kind: f.kind,
            name: f.name.trim(),
            specialty: f.specialty.trim(),
            clinic: doctor ? f.clinic.trim() : '',
            phone: f.phone.trim(),
            email: f.email.trim(),
            address: f.address.trim(),
            note: f.note.trim(),
          })
        }
      >
        <div className="k-fields">
          <label className="k-field">
            <span>Kind</span>
            <select value={f.kind} onChange={set('kind')}>
              <option value="doctor">Doctor</option>
              <option value="clinic">Clinic</option>
            </select>
          </label>
          <label className="k-field">
            <span>Name</span>
            <input required maxLength={120} value={f.name} onChange={set('name')} />
          </label>
        </div>
        <div className="k-fields">
          <label className="k-field">
            <span>{doctor ? 'Specialty' : 'What they do'}</span>
            <input
              maxLength={80}
              placeholder={doctor ? 'Endocrinologist' : 'Lab, dental, hospital'}
              value={f.specialty}
              onChange={set('specialty')}
            />
          </label>
          {doctor && (
            <label className="k-field">
              <span>Clinic</span>
              <input maxLength={120} value={f.clinic} onChange={set('clinic')} />
            </label>
          )}
        </div>
        <div className="k-fields">
          <label className="k-field">
            <span>Phone</span>
            <input type="tel" maxLength={40} value={f.phone} onChange={set('phone')} />
          </label>
          <label className="k-field">
            <span>Email</span>
            <input type="email" maxLength={254} value={f.email} onChange={set('email')} />
          </label>
        </div>
        <label className="k-field">
          <span>Address</span>
          <input maxLength={300} value={f.address} onChange={set('address')} />
        </label>
        <label className="k-field">
          <span>Note</span>
          <input maxLength={1000} value={f.note} onChange={set('note')} />
        </label>
        <ErrorLine error={save.error} />
        <Actions busy={save.isPending} label={provider ? 'Save' : 'Add'} onCancel={onClose} />
      </Form>
    </Sheet>
  );
}

export function VisitSheet({ visit, onClose }: { visit?: MedicalVisit; onClose: () => void }) {
  const first = useFirstPerson();
  const providers = useProviders().data ?? [];
  const [f, setF] = useState({
    person_id: visit?.person_id ?? first,
    provider_id: visit?.provider_id ?? '',
    date: visit?.date ?? todayLocal(),
    reason: visit?.reason ?? '',
    outcome: visit?.outcome ?? '',
    follow_up: visit?.follow_up ?? '',
  });
  const save = useSaveVisit(visit?.id ?? null, onClose);
  const set = (k: keyof typeof f) => (e: Change) => setF({ ...f, [k]: e.target.value });
  return (
    <Sheet title={visit ? 'Edit the visit' : 'New visit'} onClose={onClose} width={480}>
      <Form
        onSubmit={() =>
          save.mutate({
            person_id: f.person_id,
            provider_id: orNull(f.provider_id),
            date: f.date,
            reason: f.reason.trim(),
            outcome: f.outcome.trim(),
            follow_up: orNull(f.follow_up),
          })
        }
      >
        <div className="k-fields">
          <PersonField value={f.person_id} onChange={set('person_id')} />
          <label className="k-field">
            <span>Date</span>
            <input type="date" required value={f.date} onChange={set('date')} />
          </label>
        </div>
        <label className="k-field">
          <span>Reason</span>
          <input
            required
            maxLength={200}
            placeholder="Annual checkup"
            value={f.reason}
            onChange={set('reason')}
          />
        </label>
        <label className="k-field">
          <span>Doctor or clinic</span>
          <select value={f.provider_id} onChange={set('provider_id')}>
            <option value="">Not set</option>
            {providers.map((p) => (
              <option key={p.id} value={p.id}>
                {p.name}
              </option>
            ))}
          </select>
        </label>
        <label className="k-field">
          <span>Outcome</span>
          <textarea
            maxLength={2000}
            rows={3}
            placeholder="What the doctor said, what was prescribed"
            value={f.outcome}
            onChange={set('outcome')}
          />
        </label>
        <label className="k-field">
          <span>Follow-up</span>
          <input type="date" value={f.follow_up} onChange={set('follow_up')} />
        </label>
        <ErrorLine error={save.error} />
        <Actions busy={save.isPending} label={visit ? 'Save' : 'Add'} onCancel={onClose} />
      </Form>
    </Sheet>
  );
}

export function RecordSheet({
  record,
  onClose,
  onCreated,
}: {
  record?: MedicalRecord;
  onClose: () => void;
  /** A new record opens with its files right after the save. */
  onCreated?: (r: MedicalRecord) => void;
}) {
  const first = useFirstPerson();
  const visits = useVisits().data ?? [];
  const [f, setF] = useState({
    person_id: record?.person_id ?? first,
    visit_id: record?.visit_id ?? '',
    date: record?.date ?? todayLocal(),
    kind: record?.kind ?? ('lab' as MedicalRecord['kind']),
    title: record?.title ?? '',
    note: record?.note ?? '',
  });
  const save = useSaveRecord(record?.id ?? null, (r) => {
    if (!record) onCreated?.(r);
    onClose();
  });
  const set = (k: keyof typeof f) => (e: Change) => setF({ ...f, [k]: e.target.value });
  const theirVisits = visits.filter((v) => v.person_id === f.person_id);
  return (
    <Sheet title={record ? 'Edit the record' : 'New record'} onClose={onClose} width={480}>
      <Form
        onSubmit={() =>
          save.mutate({
            person_id: f.person_id,
            visit_id: orNull(f.visit_id),
            date: f.date,
            kind: f.kind,
            title: f.title.trim(),
            note: f.note.trim(),
          })
        }
      >
        <div className="k-fields">
          <PersonField
            value={f.person_id}
            onChange={(e) => setF({ ...f, person_id: e.target.value, visit_id: '' })}
          />
          <label className="k-field">
            <span>Date</span>
            <input type="date" required value={f.date} onChange={set('date')} />
          </label>
        </div>
        <div className="k-fields">
          <label className="k-field">
            <span>Kind</span>
            <select value={f.kind} onChange={set('kind')}>
              {RECORD_KINDS.map((k) => (
                <option key={k} value={k}>
                  {RECORD_KIND[k]}
                </option>
              ))}
            </select>
          </label>
          <label className="k-field">
            <span>From the visit</span>
            <select value={f.visit_id} onChange={set('visit_id')}>
              <option value="">None</option>
              {theirVisits.map((v) => (
                <option key={v.id} value={v.id}>
                  {`${v.date} · ${v.reason}`}
                </option>
              ))}
            </select>
          </label>
        </div>
        <label className="k-field">
          <span>Title</span>
          <input
            required
            maxLength={200}
            placeholder="Complete blood count"
            value={f.title}
            onChange={set('title')}
          />
        </label>
        <label className="k-field">
          <span>Note</span>
          <textarea maxLength={2000} rows={3} value={f.note} onChange={set('note')} />
        </label>
        {!record && <p className="k-sub">Scans go on the record once it is saved.</p>}
        <ErrorLine error={save.error} />
        <Actions busy={save.isPending} label={record ? 'Save' : 'Add'} onCancel={onClose} />
      </Form>
    </Sheet>
  );
}

export function MedicationSheet({
  medication,
  onClose,
}: {
  medication?: MedicalMedication;
  onClose: () => void;
}) {
  const first = useFirstPerson();
  const [f, setF] = useState({
    person_id: medication?.person_id ?? first,
    name: medication?.name ?? '',
    dose: medication?.dose ?? '',
    schedule: medication?.schedule ?? '',
    start_date: medication?.start_date ?? '',
    end_date: medication?.end_date ?? '',
    note: medication?.note ?? '',
  });
  const save = useSaveMedication(medication?.id ?? null, onClose);
  const set = (k: keyof typeof f) => (e: Change) => setF({ ...f, [k]: e.target.value });
  return (
    <Sheet
      title={medication ? 'Edit the medication' : 'New medication'}
      onClose={onClose}
      width={480}
    >
      <Form
        onSubmit={() =>
          save.mutate({
            person_id: f.person_id,
            name: f.name.trim(),
            dose: f.dose.trim(),
            schedule: f.schedule.trim(),
            start_date: orNull(f.start_date),
            end_date: orNull(f.end_date),
            note: f.note.trim(),
          })
        }
      >
        <div className="k-fields">
          <PersonField value={f.person_id} onChange={set('person_id')} />
          <label className="k-field">
            <span>Name</span>
            <input required maxLength={120} value={f.name} onChange={set('name')} />
          </label>
        </div>
        <div className="k-fields">
          <label className="k-field">
            <span>Dose</span>
            <input
              maxLength={80}
              placeholder="as prescribed"
              value={f.dose}
              onChange={set('dose')}
            />
          </label>
          <label className="k-field">
            <span>Schedule</span>
            <input
              maxLength={200}
              placeholder="twice a day after meals"
              value={f.schedule}
              onChange={set('schedule')}
            />
          </label>
        </div>
        <div className="k-fields">
          <label className="k-field">
            <span>From</span>
            <input type="date" value={f.start_date} onChange={set('start_date')} />
          </label>
          <label className="k-field">
            <span>Until</span>
            <input
              type="date"
              min={f.start_date || undefined}
              value={f.end_date}
              onChange={set('end_date')}
            />
          </label>
        </div>
        <label className="k-field">
          <span>Note</span>
          <input maxLength={1000} value={f.note} onChange={set('note')} />
        </label>
        <ErrorLine error={save.error} />
        <Actions busy={save.isPending} label={medication ? 'Save' : 'Add'} onCancel={onClose} />
      </Form>
    </Sheet>
  );
}

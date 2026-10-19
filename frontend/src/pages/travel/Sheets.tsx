import { useState, type FormEvent } from 'react';

import {
  useSaveContact,
  useSaveCrossing,
  useSaveDocument,
  useSaveItem,
  useSaveMoment,
  useSaveRule,
  useSaveTrip,
} from '@/hooks/useTravel';
import {
  contactBody,
  crossingBody,
  itemBody,
  momentBody,
  ruleBody,
  tripBody,
  type ContactForm,
  type ItemForm,
  type MomentForm,
  type TripForm,
} from '@/lib/travel/bodies';
import {
  PURPOSE,
  type TravelCrossing,
  type TravelDocument,
  type TravelPurpose,
  type TravelRule,
  type TravelTrip,
  type TravelTripContact,
  type TravelTripItem,
  type TravelTripMoment,
} from '@/lib/travel';
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

/** Record or edit a border crossing. */
export function CrossingSheet({
  crossing,
  initial,
  onClose,
}: {
  crossing?: TravelCrossing;
  initial?: Partial<{ date: string; from: string; to: string; time: string; place: string }>;
  onClose: () => void;
}) {
  const [f, setF] = useState({
    date: crossing?.date ?? initial?.date ?? '',
    from: crossing?.from_country ?? initial?.from ?? '',
    to: crossing?.to_country ?? initial?.to ?? '',
    time: crossing?.time ?? initial?.time ?? '',
    place: crossing?.place ?? initial?.place ?? '',
    note: crossing?.note ?? '',
  });
  const save = useSaveCrossing(crossing?.id ?? null, onClose);
  const set = (k: keyof typeof f) => (e: { target: { value: string } }) =>
    setF({ ...f, [k]: e.target.value });
  const submit = (e: FormEvent) => {
    e.preventDefault();
    save.mutate(crossingBody(f));
  };
  return (
    <Sheet
      title={crossing ? 'Edit the crossing' : 'Record a crossing'}
      onClose={onClose}
      width={460}
    >
      <form className="k-form" onSubmit={submit}>
        <label className="k-field">
          <span>Date</span>
          <input type="date" required value={f.date} onChange={set('date')} />
        </label>
        <div className="k-fields">
          <label className="k-field">
            <span>From (country code)</span>
            <input required maxLength={2} placeholder="KZ" value={f.from} onChange={set('from')} />
          </label>
          <label className="k-field">
            <span>To</span>
            <input required maxLength={2} placeholder="TH" value={f.to} onChange={set('to')} />
          </label>
        </div>
        <div className="k-fields">
          <label className="k-field">
            <span>Time</span>
            <input type="time" value={f.time} onChange={set('time')} />
          </label>
          <label className="k-field">
            <span>Where</span>
            <input maxLength={120} placeholder="Airport" value={f.place} onChange={set('place')} />
          </label>
        </div>
        <label className="k-field">
          <span>Note</span>
          <input maxLength={500} value={f.note} onChange={set('note')} />
        </label>
        <ErrorLine error={save.error} />
        <Actions busy={save.isPending} label={crossing ? 'Save' : 'Record'} onCancel={onClose} />
      </form>
    </Sheet>
  );
}

/** Create or edit a trip. */
export function TripSheet({
  trip,
  onClose,
}: {
  trip?: TravelTrip;
  onClose: (created?: TravelTrip) => void;
}) {
  const [f, setF] = useState<TripForm>({
    name: trip?.name ?? '',
    start_date: trip?.start_date ?? '',
    end_date: trip?.end_date ?? '',
    countries: (trip?.countries ?? []).join(', '),
    note: trip?.note ?? '',
    purpose: trip?.purpose ?? '',
    booked_amount: trip?.booked_cost ? String(trip.booked_cost.amount) : '',
    booked_currency: trip?.booked_cost?.currency ?? '',
  });
  const save = useSaveTrip(trip?.id ?? null, (t) => onClose(t));
  const set = (k: keyof TripForm) => (e: { target: { value: string } }) =>
    setF({ ...f, [k]: e.target.value } as TripForm);
  return (
    <Sheet title={trip ? 'Edit the trip' : 'New trip'} onClose={() => onClose()} width={460}>
      <form
        className="k-form"
        onSubmit={(e) => {
          e.preventDefault();
          save.mutate(tripBody(f));
        }}
      >
        <label className="k-field">
          <span>Name</span>
          <input
            required
            maxLength={120}
            placeholder="Thailand"
            value={f.name}
            onChange={set('name')}
          />
        </label>
        <div className="k-fields">
          <label className="k-field">
            <span>From</span>
            <input type="date" required value={f.start_date} onChange={set('start_date')} />
          </label>
          <label className="k-field">
            <span>To</span>
            <input type="date" required value={f.end_date} onChange={set('end_date')} />
          </label>
        </div>
        <div className="k-fields">
          <label className="k-field">
            <span>Countries (codes)</span>
            <input placeholder="TH, LA" value={f.countries} onChange={set('countries')} />
          </label>
          <label className="k-field">
            <span>Purpose</span>
            <select value={f.purpose} onChange={set('purpose')}>
              <option value="">Not set</option>
              {(Object.keys(PURPOSE) as TravelPurpose[]).map((k) => (
                <option key={k} value={k}>
                  {PURPOSE[k]}
                </option>
              ))}
            </select>
          </label>
        </div>
        <div className="k-fields">
          <label className="k-field">
            <span>Bookings cost</span>
            <input
              inputMode="decimal"
              placeholder="1250"
              value={f.booked_amount}
              onChange={set('booked_amount')}
            />
          </label>
          <label className="k-field">
            <span>Currency</span>
            <input
              maxLength={10}
              placeholder="USD"
              value={f.booked_currency}
              onChange={set('booked_currency')}
            />
          </label>
        </div>
        <label className="k-field">
          <span>Note</span>
          <textarea
            className="k-input k-textarea"
            maxLength={2000}
            value={f.note}
            onChange={set('note')}
          />
        </label>
        <ErrorLine error={save.error} />
        <Actions
          busy={save.isPending}
          label={trip ? 'Save' : 'Create'}
          onCancel={() => onClose()}
        />
      </form>
    </Sheet>
  );
}

/** Add or edit an item of a trip's plan. */
export function ItemSheet({
  tripId,
  item,
  date,
  onClose,
}: {
  tripId: string;
  item?: TravelTripItem;
  date: string;
  onClose: () => void;
}) {
  const [f, setF] = useState<ItemForm>({
    kind: item?.kind ?? 'flight',
    date: item?.date ?? date,
    time: item?.time ?? '',
    title: item?.title ?? '',
    reference: item?.reference ?? '',
    link: item?.link ?? '',
    note: item?.note ?? '',
    no_crossing: item?.no_crossing ?? false,
    end_date: item?.end_date ?? '',
  });
  const save = useSaveItem(tripId, item?.id ?? null, onClose);
  const set = (k: keyof ItemForm) => (e: { target: { value: string } }) =>
    setF({ ...f, [k]: e.target.value } as ItemForm);
  return (
    <Sheet title={item ? 'Edit the plan item' : 'Add to the plan'} onClose={onClose} width={460}>
      <form
        className="k-form"
        onSubmit={(e) => {
          e.preventDefault();
          save.mutate(itemBody(f));
        }}
      >
        <label className="k-field">
          <span>Kind</span>
          <select value={f.kind} onChange={set('kind')}>
            <option value="flight">Flight</option>
            <option value="stay">Stay</option>
            <option value="booking">Booking</option>
            <option value="note">Note</option>
          </select>
        </label>
        <label className="k-field">
          <span>Title</span>
          <input
            required
            maxLength={200}
            placeholder="Flight ALA to BKK"
            value={f.title}
            onChange={set('title')}
          />
        </label>
        <div className="k-fields">
          <label className="k-field">
            <span>Date</span>
            <input type="date" required value={f.date} onChange={set('date')} />
          </label>
          <label className="k-field">
            <span>Time</span>
            <input type="time" value={f.time} onChange={set('time')} />
          </label>
        </div>
        {f.kind === 'stay' && (
          <label className="k-field">
            <span>Until (the check-out day)</span>
            <input type="date" min={f.date} value={f.end_date} onChange={set('end_date')} />
          </label>
        )}
        <label className="k-field">
          <span>Booking reference</span>
          <input maxLength={80} value={f.reference} onChange={set('reference')} />
        </label>
        <label className="k-field">
          <span>Link</span>
          <input type="url" maxLength={500} value={f.link} onChange={set('link')} />
        </label>
        <label className="k-field">
          <span>Note</span>
          <input maxLength={1000} value={f.note} onChange={set('note')} />
        </label>
        {f.kind === 'flight' && (
          <label className="k-check">
            <input
              type="checkbox"
              checked={f.no_crossing}
              onChange={(e) => setF({ ...f, no_crossing: e.target.checked })}
            />{' '}
            No border crossing for me: domestic, or I am not on this flight
          </label>
        )}
        <ErrorLine error={save.error} />
        <Actions busy={save.isPending} label={item ? 'Save' : 'Add'} onCancel={onClose} />
      </form>
    </Sheet>
  );
}

/** Add or edit a person to reach on a trip. */
export function ContactSheet({
  tripId,
  contact,
  onClose,
}: {
  tripId: string;
  contact?: TravelTripContact;
  onClose: () => void;
}) {
  const [f, setF] = useState<ContactForm>({
    name: contact?.name ?? '',
    role: contact?.role ?? '',
    phone: contact?.phone ?? '',
    email: contact?.email ?? '',
    link: contact?.link ?? '',
    note: contact?.note ?? '',
  });
  const save = useSaveContact(tripId, contact?.id ?? null, onClose);
  const set = (k: keyof ContactForm) => (e: { target: { value: string } }) =>
    setF({ ...f, [k]: e.target.value });
  return (
    <Sheet title={contact ? 'Edit the contact' : 'Add a contact'} onClose={onClose} width={460}>
      <form
        className="k-form"
        onSubmit={(e) => {
          e.preventDefault();
          save.mutate(contactBody(f));
        }}
      >
        <div className="k-fields">
          <label className="k-field">
            <span>Name</span>
            <input required maxLength={120} value={f.name} onChange={set('name')} />
          </label>
          <label className="k-field">
            <span>Role</span>
            <input maxLength={80} placeholder="host, guide" value={f.role} onChange={set('role')} />
          </label>
        </div>
        <div className="k-fields">
          <label className="k-field">
            <span>Phone</span>
            <input type="tel" maxLength={40} value={f.phone} onChange={set('phone')} />
          </label>
          <label className="k-field">
            <span>Email</span>
            <input type="email" maxLength={200} value={f.email} onChange={set('email')} />
          </label>
        </div>
        <label className="k-field">
          <span>Link</span>
          <input type="url" maxLength={500} value={f.link} onChange={set('link')} />
        </label>
        <label className="k-field">
          <span>Note</span>
          <input maxLength={500} value={f.note} onChange={set('note')} />
        </label>
        <ErrorLine error={save.error} />
        <Actions busy={save.isPending} label={contact ? 'Save' : 'Add'} onCancel={onClose} />
      </form>
    </Sheet>
  );
}

/** Add or edit a moment of a trip. */
export function MomentSheet({
  tripId,
  moment,
  date,
  onClose,
}: {
  tripId: string;
  moment?: TravelTripMoment;
  date: string;
  onClose: () => void;
}) {
  const [f, setF] = useState<MomentForm>({
    date: moment?.date ?? date,
    text: moment?.text ?? '',
    place: moment?.place ?? '',
  });
  const save = useSaveMoment(tripId, moment?.id ?? null, onClose);
  const set = (k: keyof MomentForm) => (e: { target: { value: string } }) =>
    setF({ ...f, [k]: e.target.value });
  return (
    <Sheet title={moment ? 'Edit the moment' : 'Add a moment'} onClose={onClose} width={460}>
      <form
        className="k-form"
        onSubmit={(e) => {
          e.preventDefault();
          save.mutate(momentBody(f));
        }}
      >
        <div className="k-fields">
          <label className="k-field">
            <span>Date</span>
            <input type="date" required value={f.date} onChange={set('date')} />
          </label>
          <label className="k-field">
            <span>Place</span>
            <input maxLength={120} value={f.place} onChange={set('place')} />
          </label>
        </div>
        <label className="k-field">
          <span>What happened</span>
          <textarea
            className="k-input k-textarea"
            required
            maxLength={2000}
            value={f.text}
            onChange={set('text')}
          />
        </label>
        <ErrorLine error={save.error} />
        <Actions busy={save.isPending} label={moment ? 'Save' : 'Add'} onCancel={onClose} />
      </form>
    </Sheet>
  );
}

/** Create or edit a day counter rule. */
export function RuleSheet({ rule, onClose }: { rule?: TravelRule; onClose: () => void }) {
  const [f, setF] = useState({
    name: rule?.name ?? '',
    countries: (rule?.countries ?? []).join(', '),
    window_kind: rule?.window_kind ?? ('since_last_entry' as TravelRule['window_kind']),
    window_days: String(rule?.window_days ?? 180),
    threshold_days: String(rule?.threshold_days ?? 60),
    threshold_kind: rule?.threshold_kind ?? ('at_most' as TravelRule['threshold_kind']),
    count_crossing_days: rule?.count_crossing_days ?? true,
    source: rule?.source ?? '',
  });
  const save = useSaveRule(rule?.id ?? null, onClose);
  const set = (k: keyof typeof f) => (e: { target: { value: string } }) =>
    setF({ ...f, [k]: e.target.value } as typeof f);
  return (
    <Sheet title={rule ? 'Edit the rule' : 'New rule'} onClose={onClose} width={480}>
      <form
        className="k-form"
        onSubmit={(e) => {
          e.preventDefault();
          save.mutate(ruleBody(f));
        }}
      >
        <label className="k-field">
          <span>Name</span>
          <input
            required
            maxLength={80}
            placeholder="Thailand visa-free"
            value={f.name}
            onChange={set('name')}
          />
        </label>
        <label className="k-field">
          <span>Countries (codes, or SCHENGEN)</span>
          <input required placeholder="TH" value={f.countries} onChange={set('countries')} />
        </label>
        <label className="k-field">
          <span>Window</span>
          <select value={f.window_kind} onChange={set('window_kind')}>
            <option value="since_last_entry">Since the last entry</option>
            <option value="rolling_days">Any period of N days</option>
            <option value="rolling_12_months_in_year">Any 12 months ending this year</option>
            <option value="calendar_year">This calendar year</option>
          </select>
        </label>
        <div className="k-fields">
          {f.window_kind === 'rolling_days' && (
            <label className="k-field">
              <span>Days in the window</span>
              <input
                type="number"
                min={1}
                max={3650}
                value={f.window_days}
                onChange={set('window_days')}
              />
            </label>
          )}
          <label className="k-field">
            <span>Threshold, days</span>
            <input
              type="number"
              min={1}
              max={3650}
              value={f.threshold_days}
              onChange={set('threshold_days')}
            />
          </label>
        </div>
        <label className="k-field">
          <span>The threshold is</span>
          <select value={f.threshold_kind} onChange={set('threshold_kind')}>
            <option value="at_most">A limit (at most)</option>
            <option value="over">Met when over (residency)</option>
          </select>
        </label>
        <label className="k-check">
          <input
            type="checkbox"
            checked={f.count_crossing_days}
            onChange={(e) => setF({ ...f, count_crossing_days: e.target.checked })}
          />
          Count the days of entry and exit
        </label>
        <label className="k-field">
          <span>Rule and source</span>
          <textarea
            className="k-input k-textarea"
            maxLength={600}
            value={f.source}
            onChange={set('source')}
          />
        </label>
        <ErrorLine error={save.error} />
        <Actions busy={save.isPending} label={rule ? 'Save' : 'Add'} onCancel={onClose} />
      </form>
    </Sheet>
  );
}

/** Add or edit a travel document. */
export function DocumentSheet({
  document,
  onClose,
}: {
  document?: TravelDocument;
  onClose: () => void;
}) {
  const [f, setF] = useState({
    holder: document?.holder ?? '',
    kind: document?.kind ?? ('passport' as TravelDocument['kind']),
    number: document?.number ?? '',
    valid_until: document?.valid_until ?? '',
    note: document?.note ?? '',
  });
  const save = useSaveDocument(document?.id ?? null, onClose);
  const set = (k: keyof typeof f) => (e: { target: { value: string } }) =>
    setF({ ...f, [k]: e.target.value });
  return (
    <Sheet title={document ? 'Edit the document' : 'New document'} onClose={onClose} width={440}>
      <form
        className="k-form"
        onSubmit={(e) => {
          e.preventDefault();
          save.mutate({
            holder: f.holder.trim(),
            kind: f.kind,
            number: f.number.trim(),
            valid_until: f.valid_until,
            note: f.note.trim(),
          });
        }}
      >
        <div className="k-fields">
          <label className="k-field">
            <span>Whose</span>
            <input
              required
              maxLength={80}
              placeholder="Olesia"
              value={f.holder}
              onChange={set('holder')}
            />
          </label>
          <label className="k-field">
            <span>Kind</span>
            <select value={f.kind} onChange={set('kind')}>
              <option value="passport">Passport</option>
              <option value="visa">Visa</option>
              <option value="insurance">Insurance</option>
              <option value="other">Other</option>
            </select>
          </label>
        </div>
        <div className="k-fields">
          <label className="k-field">
            <span>Number</span>
            <input maxLength={60} value={f.number} onChange={set('number')} />
          </label>
          <label className="k-field">
            <span>Valid until</span>
            <input type="date" required value={f.valid_until} onChange={set('valid_until')} />
          </label>
        </div>
        <label className="k-field">
          <span>Note</span>
          <input maxLength={500} value={f.note} onChange={set('note')} />
        </label>
        <ErrorLine error={save.error} />
        <Actions busy={save.isPending} label={document ? 'Save' : 'Add'} onCancel={onClose} />
      </form>
    </Sheet>
  );
}

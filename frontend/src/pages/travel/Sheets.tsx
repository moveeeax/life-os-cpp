import { useState, type FormEvent } from 'react';

import { useSaveCrossing, useSaveItem, useSaveRule, useSaveTrip } from '@/hooks/useTravel';
import { crossingBody, itemBody, ruleBody, tripBody, type ItemForm } from '@/lib/travel/bodies';
import type { TravelCrossing, TravelRule, TravelTrip, TravelTripItem } from '@/lib/travel';
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
  const [f, setF] = useState({
    name: trip?.name ?? '',
    start_date: trip?.start_date ?? '',
    end_date: trip?.end_date ?? '',
    countries: (trip?.countries ?? []).join(', '),
    note: trip?.note ?? '',
  });
  const save = useSaveTrip(trip?.id ?? null, (t) => onClose(t));
  const set = (k: keyof typeof f) => (e: { target: { value: string } }) =>
    setF({ ...f, [k]: e.target.value });
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
        <label className="k-field">
          <span>Countries (codes)</span>
          <input placeholder="TH, LA" value={f.countries} onChange={set('countries')} />
        </label>
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
        <ErrorLine error={save.error} />
        <Actions busy={save.isPending} label={item ? 'Save' : 'Add'} onCancel={onClose} />
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

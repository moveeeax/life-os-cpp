import { useState } from 'react';
import { useNavigate, useParams } from 'react-router';

import { useCreateTask, useCreateTasks, useUpdateTask } from '@/hooks/useTasks';
import {
  useCandidateSpends,
  useCrossings,
  useDeleteItem,
  useDeleteTrip,
  useLinkSpends,
  usePacking,
  useTrip,
  useTrips,
} from '@/hooks/useTravel';
import { useErrorToast } from '@/hooks/useErrorToast';
import { formatTotal } from '@/lib/networth';
import { safeLink, type Task } from '@/lib/tasks';
import {
  countdownWords,
  countryName,
  daysBetween,
  documentWarningLine,
  flightsWithoutCrossing,
  type TravelTrip,
  type TravelTripItem,
  shortDay,
  todayLocal,
} from '@/lib/travel';
import { Banner, Facts, Group, Num, Row, Sheet } from '@/shell/kit';
import { ConfirmDialog } from '@/components/ConfirmDialog';

import { LoadError, Placeholder, TravelFrame } from './frame';
import { CrossingSheet, ItemSheet, TripSheet } from './Sheets';

type Tab = 'plan' | 'packing' | 'spends';

function weekday(iso: string) {
  return new Date(`${iso}T00:00:00Z`).toLocaleDateString('en-US', {
    weekday: 'short',
    timeZone: 'UTC',
  });
}

function Plan({ trip, today }: { trip: TravelTrip; today: string }) {
  const crossings = useCrossings();
  const remove = useDeleteItem(trip.id);
  useErrorToast(remove.error);
  const [editing, setEditing] = useState<TravelTripItem | 'new' | null>(null);
  const [crossingFrom, setCrossingFrom] = useState<TravelTripItem | null>(null);
  const items = trip.items ?? [];
  const missing = flightsWithoutCrossing(items, crossings.data ?? []);
  const tripDays = daysBetween(trip.start_date, trip.end_date) + 1;
  const byDay = new Map<string, TravelTripItem[]>();
  for (const i of items) byDay.set(i.date, [...(byDay.get(i.date) ?? []), i]);
  return (
    <>
      <div className="k-tools">
        <span className="k-grow" />
        <button type="button" className="k-btn k-solid" onClick={() => setEditing('new')}>
          Add to plan
        </button>
      </div>
      {(trip.document_warnings ?? []).map((w) => (
        <Banner key={w.document_id} to="/travel/documents" action="Documents">
          {documentWarningLine(w, today)}
        </Banner>
      ))}
      {missing.slice(0, 1).map((f) => (
        <Banner key={f.id} action="Record" onAction={() => setCrossingFrom(f)}>
          {f.title} on {shortDay(f.date, today)} has no crossing yet
        </Banner>
      ))}
      {(trip.allowed ?? []).length > 0 && (
        <Facts
          items={(trip.allowed ?? []).map((a) => ({
            label: a.rule,
            value: (
              <Num>
                <span className={a.days < tripDays ? 'k-warn-text' : undefined}>
                  {`up to ${a.days} days in a row from ${shortDay(trip.start_date, today)}`}
                </span>
              </Num>
            ),
          }))}
        />
      )}
      {items.length === 0 && (
        <p className="k-empty">Nothing planned yet: flights, stays, bookings, notes.</p>
      )}
      {[...byDay.entries()].map(([day, list]) => (
        <Group key={day} label={`${weekday(day)} · ${shortDay(day, today)}`}>
          {list.map((i) => (
            <Row
              key={i.id}
              dot={
                i.kind === 'flight'
                  ? 'var(--g-brand)'
                  : i.kind === 'stay'
                    ? 'var(--g-ok)'
                    : 'var(--g-ink-faint)'
              }
              title={i.title}
              sub={[i.kind, i.time].filter(Boolean).join(' · ')}
            >
              {i.reference && (
                <span>
                  Booking <span className="num">{i.reference}</span>
                </span>
              )}
              <ItemLink link={i.link} />
              {i.note && <span>{i.note}</span>}
              <span className="k-actions">
                <button type="button" className="k-link" onClick={() => setEditing(i)}>
                  Edit
                </button>
                <button
                  type="button"
                  className="k-link k-error-text"
                  onClick={() => remove.mutate(i.id)}
                >
                  Delete
                </button>
              </span>
            </Row>
          ))}
        </Group>
      ))}
      {editing && (
        <ItemSheet
          tripId={trip.id}
          item={editing === 'new' ? undefined : editing}
          date={trip.start_date}
          onClose={() => setEditing(null)}
        />
      )}
      {crossingFrom && (
        <CrossingSheet
          initial={{
            date: crossingFrom.date,
            time: crossingFrom.time ?? '',
            to: trip.countries[0] ?? '',
          }}
          onClose={() => setCrossingFrom(null)}
        />
      )}
    </>
  );
}

function PackingRow({ task }: { task: Task }) {
  const update = useUpdateTask(task.id);
  useErrorToast(update.error);
  const done = task.status === 'done';
  return (
    <Row
      dot={done ? 'var(--g-ok)' : 'var(--g-warn)'}
      title={<span className={done ? 'k-muted k-strike' : undefined}>{task.title}</span>}
      sub={task.due ? `due ${task.due}` : undefined}
      right={
        <button
          type="button"
          className="k-link"
          aria-label={done ? `Reopen: ${task.title}` : `Done: ${task.title}`}
          onClick={() => update.mutate({ status: done ? 'open' : 'done' })}
        >
          {done ? 'undo' : 'done'}
        </button>
      }
    />
  );
}

function Packing({ trip }: { trip: TravelTrip }) {
  const packing = usePacking(trip.id);
  const trips = useTrips();
  const [text, setText] = useState('');
  const create = useCreateTask(() => setText(''));
  const copy = useCreateTasks();
  useErrorToast(create.error ?? copy.error);
  const [copying, setCopying] = useState(false);
  const others = (trips.data ?? []).filter((t) => t.id !== trip.id && t.packing.total > 0);
  const add = (title: string) =>
    create.mutate({
      title: title.slice(0, 200),
      area: 'travel',
      trip_id: trip.id,
      due: trip.start_date,
    });
  return (
    <>
      <form
        className="k-capture-form"
        onSubmit={(e) => {
          e.preventDefault();
          if (text.trim()) add(text.trim());
        }}
      >
        <input
          className="k-input"
          aria-label="Add a thing to pack"
          placeholder="Add a thing, e.g. power adapter"
          value={text}
          onChange={(e) => setText(e.target.value)}
        />
        <button type="submit" className="k-btn" disabled={create.isPending}>
          +
        </button>
        {others.length > 0 && (
          <button type="button" className="k-btn" onClick={() => setCopying(true)}>
            Copy from
          </button>
        )}
      </form>
      {packing.isPending ? (
        <Placeholder />
      ) : packing.isError ? (
        <LoadError error={packing.error} onRetry={() => void packing.refetch()} />
      ) : (packing.data ?? []).length === 0 ? (
        <p className="k-empty">Nothing to pack yet. Each thing is a task in Tasks, area Travel.</p>
      ) : (
        <Group label="Packing" total={<Num>{`${trip.packing.done} of ${trip.packing.total}`}</Num>}>
          {(packing.data ?? []).map((t) => (
            <PackingRow key={t.id} task={t} />
          ))}
        </Group>
      )}
      {copying && (
        <CopyPacking
          from={others}
          onPick={(titles) => {
            copy.mutate(
              titles.map((title) => ({
                title,
                area: 'travel',
                trip_id: trip.id,
                due: trip.start_date,
              })),
            );
            setCopying(false);
          }}
          onClose={() => setCopying(false)}
        />
      )}
    </>
  );
}

function CopyPacking({
  from,
  onPick,
  onClose,
}: {
  from: TravelTrip[];
  onPick: (titles: string[]) => void;
  onClose: () => void;
}) {
  const [source, setSource] = useState(from[0]?.id ?? '');
  const tasks = usePacking(source);
  return (
    <Sheet title="Copy packing from a trip" onClose={onClose} width={440}>
      <div className="k-form">
        <label className="k-field">
          <span>Trip</span>
          <select value={source} onChange={(e) => setSource(e.target.value)}>
            {from.map((t) => (
              <option key={t.id} value={t.id}>
                {t.name}
              </option>
            ))}
          </select>
        </label>
        <p className="k-sub">
          {(tasks.data ?? []).map((t) => t.title).join(', ') || 'Nothing to copy.'}
        </p>
        <div className="k-dlg-actions">
          <button type="button" className="k-btn" onClick={onClose}>
            Cancel
          </button>
          <button
            type="button"
            className="k-btn k-solid"
            disabled={!tasks.data?.length}
            onClick={() => onPick((tasks.data ?? []).map((t) => t.title))}
          >
            Copy
          </button>
        </div>
      </div>
    </Sheet>
  );
}

function Spends({ trip }: { trip: TravelTrip }) {
  const [picking, setPicking] = useState(false);
  const link = useLinkSpends(trip.id, () => setPicking(false));
  useErrorToast(link.error);
  const rows = trip.spend_rows ?? [];
  const total = trip.spends.total;
  return (
    <>
      <div className="k-tools">
        <span className="k-grow" />
        <button type="button" className="k-btn k-solid" onClick={() => setPicking(true)}>
          Link spends of these dates
        </button>
      </div>
      {rows.length === 0 ? (
        <p className="k-empty">No spends linked yet.</p>
      ) : (
        <>
          <Facts
            items={[
              ...trip.spends.by_currency.map((c) => ({
                label: c.currency,
                value: <Num>{formatTotal(c.amount)}</Num>,
              })),
              {
                label: `About, in ${total.currency}`,
                value: <Num>{formatTotal(total.amount)}</Num>,
              },
            ]}
          />
          <p className="k-sub">
            {total.rate_date ? `At the rates of each spend's day, from ${total.rate_date} on.` : ''}
            {total.partial ? ' Some spends have no rate and are left out of the total.' : ''}
          </p>
          <Group label="Spends">
            {rows.map((s) => (
              <Row
                key={s.id}
                title={s.name}
                sub={s.date}
                right={
                  <Num>
                    {formatTotal(s.amount)} {s.currency}
                  </Num>
                }
              >
                <span className="k-actions">
                  <button
                    type="button"
                    className="k-link"
                    onClick={() => link.mutate({ unlink: [s.id] })}
                  >
                    Unlink
                  </button>
                </span>
              </Row>
            ))}
          </Group>
        </>
      )}
      {picking && (
        <PickSpends
          trip={trip}
          onLink={(ids) => link.mutate({ link: ids })}
          onClose={() => setPicking(false)}
        />
      )}
    </>
  );
}

function PickSpends({
  trip,
  onLink,
  onClose,
}: {
  trip: TravelTrip;
  onLink: (ids: string[]) => void;
  onClose: () => void;
}) {
  const candidates = useCandidateSpends(trip.id, true);
  const [ticked, setTicked] = useState<Set<string> | null>(null);
  const list = candidates.data ?? [];
  const chosen = ticked ?? new Set(list.filter((s) => !s.linked).map((s) => s.id));
  const toggle = (id: string) => {
    const next = new Set(chosen);
    if (next.has(id)) next.delete(id);
    else next.add(id);
    setTicked(next);
  };
  return (
    <Sheet title="Link spends of the trip dates" onClose={onClose} width={520}>
      {candidates.isPending ? (
        <Placeholder />
      ) : list.length === 0 ? (
        <p className="k-empty">No posted spends on these dates.</p>
      ) : (
        <div className="k-group-rows">
          {list.map((s) => (
            <label key={s.id} className="k-kv k-cov">
              <span>
                <input
                  type="checkbox"
                  checked={chosen.has(s.id)}
                  onChange={() => toggle(s.id)}
                  disabled={s.linked}
                />{' '}
                {s.name} <span className="k-muted">{s.date}</span>
              </span>
              <Num>
                {formatTotal(s.amount)} {s.currency}
              </Num>
            </label>
          ))}
        </div>
      )}
      <div className="k-dlg-actions">
        <button type="button" className="k-btn" onClick={onClose}>
          Cancel
        </button>
        <button
          type="button"
          className="k-btn k-solid"
          disabled={[...chosen].filter((id) => !list.find((s) => s.id === id)?.linked).length === 0}
          onClick={() => onLink([...chosen].filter((id) => !list.find((s) => s.id === id)?.linked))}
        >
          Link
        </button>
      </div>
    </Sheet>
  );
}

/** One trip: its plan by day, packing and spends. */
export function TravelTripPage() {
  const { id = '' } = useParams();
  const today = todayLocal();
  const trip = useTrip(id);
  const [tab, setTab] = useState<Tab>('plan');
  const [editing, setEditing] = useState(false);
  const [confirm, setConfirm] = useState(false);
  const navigate = useNavigate();
  const remove = useDeleteTrip(() => navigate('/travel'));
  useErrorToast(remove.error);
  const t = trip.data;
  const days = t ? daysBetween(today, t.start_date) : 0;
  return (
    <TravelFrame
      title="Trip"
      headline={t ? t.name : 'Trip'}
      sub={
        t ? (
          <>
            {shortDay(t.start_date, today)} – {shortDay(t.end_date, today)}
            {t.start_date > today && <span data-num-ok> · {countdownWords(days)}</span>}
            {t.countries.length > 0 && ` · ${t.countries.map(countryName).join(', ')}`}
          </>
        ) : undefined
      }
      actions={
        t ? (
          <>
            <div className="k-seg" role="tablist" aria-label="Trip">
              {(['plan', 'packing', 'spends'] as const).map((k) => (
                <button
                  key={k}
                  type="button"
                  role="tab"
                  aria-selected={tab === k}
                  className={tab === k ? 'k-seg-on' : undefined}
                  onClick={() => setTab(k)}
                >
                  {k === 'plan' ? 'Plan' : k === 'packing' ? 'Packing' : 'Spends'}
                </button>
              ))}
            </div>
            <span className="k-grow" />
            <button type="button" className="k-btn" onClick={() => setEditing(true)}>
              Edit
            </button>
            <button type="button" className="k-btn k-danger" onClick={() => setConfirm(true)}>
              Delete
            </button>
          </>
        ) : undefined
      }
    >
      {trip.isPending ? (
        <Placeholder />
      ) : trip.isError ? (
        <LoadError
          error={trip.error}
          onRetry={() => void trip.refetch()}
          notFound="This trip is not here: it may have been deleted."
        />
      ) : tab === 'plan' ? (
        <Plan trip={trip.data} today={today} />
      ) : tab === 'packing' ? (
        <Packing trip={trip.data} />
      ) : (
        <Spends trip={trip.data} />
      )}
      {editing && t && <TripSheet trip={t} onClose={() => setEditing(false)} />}
      {confirm && t && (
        <ConfirmDialog
          title="Delete the trip?"
          description="Its plan goes; its packing tasks and linked spends stay, unlinked."
          confirmLabel="Delete"
          destructive
          busy={remove.isPending}
          onConfirm={() => remove.mutate(t.id)}
          onClose={() => setConfirm(false)}
        />
      )}
    </TravelFrame>
  );
}

/** A booking link, only when it is a web address. */
function ItemLink({ link }: { link: string }) {
  const href = safeLink(link);
  return href ? (
    <a className="k-link" href={href} target="_blank" rel="noreferrer">
      {link}
    </a>
  ) : null;
}

import { lazy, Suspense, useMemo, useState } from 'react';
import { Link, useNavigate } from 'react-router';

import { useTrips } from '@/hooks/useTravel';
import {
  countdownWords,
  countryName,
  daysBetween,
  daysWords,
  headlineTrip,
  splitTrips,
  tripDays,
  tripsOfYear,
  tripYears,
  type TravelTrip,
  shortDay,
  todayLocal,
  yearSummary,
} from '@/lib/travel';
import { formatTotal } from '@/lib/networth';
import { Num, Row } from '@/shell/kit';

import { LoadError, Placeholder, TravelFrame } from './frame';
import { TripSheet } from './Sheets';
import { YearStrip } from './YearStrip';

import './trips.css';

const TripsMap = lazy(() => import('./TripsMap'));
const VIEW_KEY = 'travel:trips:view';
type View = 'table' | 'map';

function savedView(): View {
  try {
    return globalThis.localStorage?.getItem(VIEW_KEY) === 'table' ? 'table' : 'map';
  } catch {
    return 'map';
  }
}

function dates(t: TravelTrip, today: string) {
  if (t.start_date === t.end_date) return shortDay(t.start_date, today);
  return `${shortDay(t.start_date, today)} – ${shortDay(t.end_date, today)}`;
}

function TripRow({
  trip,
  today,
  year,
  open,
  onToggle,
}: {
  trip: TravelTrip;
  today: string;
  year: number;
  open: boolean;
  onToggle: (open: boolean) => void;
}) {
  const d = tripDays(trip, today, year);
  const places = trip.countries.map(countryName).join(', ');
  const total = trip.spends.total;
  return (
    <Row
      dot={d.live ? 'var(--g-brand)' : d.future ? 'transparent' : 'rgba(255, 255, 255, 0.45)'}
      title={trip.name}
      right={
        <span data-num-ok className={d.live ? 't-nowtxt' : undefined}>
          {d.live ? 'now · ' : ''}
          {dates(trip, today)} ·{' '}
          {d.future ? countdownWords(daysBetween(today, trip.start_date)) : daysWords(d)}
        </span>
      }
      open={open}
      onToggle={onToggle}
    >
      {places && <span>{places}</span>}
      <span>
        Packing <Num>{trip.packing.done}</Num> of <Num>{trip.packing.total}</Num>
        {trip.spends.count > 0 && (
          <>
            {' '}
            · spends{' '}
            <Num>
              {formatTotal(total.amount)} {total.currency}
            </Num>
            {total.partial ? ' (some without a rate)' : ''}
          </>
        )}
      </span>
      {trip.note && <span>{trip.note}</span>}
      <span className="k-actions">
        <Link to={`/travel/trips/${trip.id}`} className="k-link">
          Open the trip
        </Link>
      </span>
    </Row>
  );
}

/** The picked trip, or the picked place with all its trips of the year, under the strip. */
function Picked({ trips, today, year }: { trips: TravelTrip[]; today: string; year: number }) {
  if (trips.length === 0) return null;
  if (trips.length === 1) {
    const t = trips[0];
    const d = tripDays(t, today, year);
    return (
      <div className="t-picked">
        <Link to={`/travel/trips/${t.id}`} className="t-picked-name">
          {t.name}
        </Link>
        <span data-num-ok>
          {dates(t, today)} ·{' '}
          {d.live
            ? `${d.done} of ${d.plan} days so far`
            : d.future
              ? countdownWords(daysBetween(today, t.start_date))
              : d.plan
                ? `${d.plan} days`
                : 'day trip'}
        </span>
      </div>
    );
  }
  const days = trips.reduce((s, t) => s + tripDays(t, today, year).done, 0);
  return (
    <div className="t-picked">
      <span className="t-picked-name">{trips.map((t) => t.name).join(', ')}</span>
      <span data-num-ok>
        {trips.length} trips · {days} days
      </span>
    </div>
  );
}

/** Coming and past trips; the next one as the headline. */
export function TravelTripsPage() {
  const today = todayLocal();
  const trips = useTrips();
  const [adding, setAdding] = useState(false);
  const navigate = useNavigate();
  const all = useMemo(() => trips.data ?? [], [trips.data]);
  const { coming } = splitTrips(all, today);
  // A running trip wins over the next one ahead.
  const next = headlineTrip(coming, today);
  const years = useMemo(() => tripYears(all, today), [all, today]);
  const [pickedYear, setYear] = useState<number | null>(null);
  const year = pickedYear && years.includes(pickedYear) ? pickedYear : years[0];
  const ofYear = useMemo(() => (year ? tripsOfYear(all, year) : []), [all, year]);
  const [view, setViewState] = useState<View>(savedView);
  const setView = (v: View) => {
    setViewState(v);
    try {
      globalThis.localStorage?.setItem(VIEW_KEY, v);
    } catch {
      // Remembering the view is a convenience.
    }
  };
  const [picked, setPicked] = useState<string[] | null>(null);
  // Until something is picked: the trip going on, else the year's latest one.
  const fallback = useMemo(() => {
    const live = ofYear.find((t) => tripDays(t, today).live);
    const started = ofYear.filter((t) => t.start_date <= today);
    const pick = live ?? started[started.length - 1] ?? ofYear[0];
    return pick ? [pick.id] : [];
  }, [ofYear, today]);
  const selected = picked?.filter((id) => ofYear.some((t) => t.id === id)) ?? [];
  const sel = selected.length > 0 ? selected : view === 'map' ? fallback : [];
  const summary = year ? yearSummary(all, today, year) : { trips: 0, days: 0 };
  return (
    <TravelFrame
      title="Trips"
      headline={
        next ? (
          next.start_date <= today ? (
            `${next.name}, now`
          ) : (
            <>
              {next.name}{' '}
              <span data-num-ok>{countdownWords(daysBetween(today, next.start_date))}</span>
            </>
          )
        ) : (
          'Travel'
        )
      }
      sub={
        next ? (
          <>
            {dates(next, today)} · <Num>{next.packing.done}</Num> of <Num>{next.packing.total}</Num>{' '}
            things packed
          </>
        ) : undefined
      }
      actions={
        <>
          <span className="k-grow" />
          <button type="button" className="k-btn k-solid" onClick={() => setAdding(true)}>
            New trip
          </button>
        </>
      }
    >
      {trips.isPending ? (
        <Placeholder />
      ) : trips.isError ? (
        <LoadError error={trips.error} onRetry={() => void trips.refetch()} />
      ) : all.length === 0 ? (
        <p className="k-empty">
          No trips yet. A trip keeps its plan by day, its packing and its spends.
        </p>
      ) : (
        <>
          <div className="t-ctl">
            <div className="k-seg k-seg-scroll" role="group" aria-label="Year">
              {years.map((y) => (
                <button
                  key={y}
                  type="button"
                  aria-pressed={y === year}
                  className={y === year ? 'k-seg-on' : undefined}
                  onClick={() => {
                    setYear(y);
                    setPicked(null);
                  }}
                  data-num-ok
                >
                  {y}
                </button>
              ))}
            </div>
            <div className="k-seg" role="group" aria-label="View">
              {(['table', 'map'] as const).map((v) => (
                <button
                  key={v}
                  type="button"
                  aria-pressed={v === view}
                  className={v === view ? 'k-seg-on' : undefined}
                  onClick={() => setView(v)}
                >
                  {v === 'table' ? 'Table' : 'Map'}
                </button>
              ))}
            </div>
          </div>
          <p className="k-sub" data-num-ok>
            {summary.trips === 1 ? '1 trip' : `${summary.trips} trips`} · {summary.days} days away
          </p>
          <YearStrip
            year={year}
            trips={ofYear}
            today={today}
            selected={sel}
            onPick={(id) => setPicked(sel.length === 1 && sel[0] === id ? [] : [id])}
          />
          {view === 'table' ? (
            <div className="k-group-rows">
              {[...ofYear].reverse().map((t) => (
                <TripRow
                  key={t.id}
                  trip={t}
                  today={today}
                  year={year}
                  open={sel.includes(t.id)}
                  onToggle={(open) => setPicked(open ? [t.id] : sel.filter((id) => id !== t.id))}
                />
              ))}
            </div>
          ) : (
            <>
              <Picked trips={ofYear.filter((t) => sel.includes(t.id))} today={today} year={year} />
              <Suspense fallback={<div className="t-map-ph" aria-hidden="true" />}>
                <TripsMap
                  year={year}
                  trips={ofYear}
                  today={today}
                  selected={sel}
                  onPick={(ids) => setPicked(ids)}
                />
              </Suspense>
            </>
          )}
        </>
      )}
      {adding && (
        <TripSheet
          onClose={(created) => {
            setAdding(false);
            if (created) navigate(`/travel/trips/${created.id}`);
          }}
        />
      )}
    </TravelFrame>
  );
}

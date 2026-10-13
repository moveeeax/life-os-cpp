import { useState } from 'react';
import { Link, useNavigate } from 'react-router';

import { useTrips } from '@/hooks/useTravel';
import {
  countdownWords,
  countryName,
  daysBetween,
  splitTrips,
  type TravelTrip,
  shortDay,
  todayLocal,
} from '@/lib/travel';
import { formatTotal } from '@/lib/networth';
import { Group, Num, Row } from '@/shell/kit';

import { LoadError, Placeholder, TravelFrame } from './frame';
import { TripSheet } from './Sheets';

function dates(t: TravelTrip, today: string) {
  return `${shortDay(t.start_date, today)} – ${shortDay(t.end_date, today)}`;
}

function TripRow({ trip, today, coming }: { trip: TravelTrip; today: string; coming: boolean }) {
  const days = daysBetween(today, trip.start_date);
  const running = trip.start_date <= today && trip.end_date >= today;
  const places = trip.countries.map(countryName).join(', ');
  const total = trip.spends.total;
  return (
    <Row
      dot={coming ? 'var(--g-brand)' : 'rgba(255, 255, 255, 0.45)'}
      title={trip.name}
      sub={[dates(trip, today), places].filter(Boolean).join(' · ')}
      right={
        coming ? (
          running ? (
            <span className="k-muted">now</span>
          ) : (
            <span data-num-ok>{countdownWords(days)}</span>
          )
        ) : trip.spends.count > 0 ? (
          <Num>
            {formatTotal(total.amount)} {total.currency}
          </Num>
        ) : undefined
      }
    >
      <span>
        Packing <Num>{trip.packing.done}</Num> of <Num>{trip.packing.total}</Num>
        {trip.spends.count > 0 && (
          <>
            {' '}
            · spends{' '}
            <Num>
              {trip.spends.by_currency
                .map((c) => `${formatTotal(c.amount)} ${c.currency}`)
                .join(', ')}
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

/** Coming and past trips; the next one as the headline. */
export function TravelTripsPage() {
  const today = todayLocal();
  const trips = useTrips();
  const [adding, setAdding] = useState(false);
  const navigate = useNavigate();
  const { coming, past } = splitTrips(trips.data ?? [], today);
  const next = coming.find((t) => t.start_date >= today) ?? coming[0];
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
      ) : coming.length + past.length === 0 ? (
        <p className="k-empty">
          No trips yet. A trip keeps its plan by day, its packing and its spends.
        </p>
      ) : (
        <>
          {coming.length > 0 && (
            <Group label="Coming">
              {coming.map((t) => (
                <TripRow key={t.id} trip={t} today={today} coming />
              ))}
            </Group>
          )}
          {past.length > 0 && (
            <Group label="Past">
              {past.map((t) => (
                <TripRow key={t.id} trip={t} today={today} coming={false} />
              ))}
            </Group>
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

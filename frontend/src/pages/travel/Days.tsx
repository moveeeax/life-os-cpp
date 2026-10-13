import { useState } from 'react';
import { Link } from 'react-router';

import { useCounters, useCrossings, useDeleteCrossing } from '@/hooks/useTravel';
import { useErrorToast } from '@/hooks/useErrorToast';
import {
  counterLine,
  countryName,
  type TravelCounter,
  type TravelCrossing,
  shortDay,
  todayLocal,
  whereNow,
} from '@/lib/travel';
import { Group, Meter, Num, Row } from '@/shell/kit';

import { LoadError, Placeholder, TravelFrame } from './frame';
import { CrossingSheet } from './Sheets';

function tone(c: TravelCounter): 'ok' | 'warn' | 'late' | undefined {
  if (c.threshold_kind === 'over') return c.left === 0 ? 'ok' : undefined;
  if (c.left === 0) return 'late';
  return c.left <= 14 ? 'warn' : 'ok';
}

function CounterRow({ c, today }: { c: TravelCounter; today: string }) {
  const limit = c.threshold_kind === 'at_most' ? c.threshold_days : c.threshold_days + 1;
  return (
    <Row
      title={c.name}
      sub={<Meter value={Math.min(c.used, limit)} max={limit} tone={tone(c)} />}
      right={<Num>{counterLine(c)}</Num>}
    >
      <span>
        {c.threshold_kind === 'at_most' ? (
          <>
            <Num>{c.left}</Num> days left
          </>
        ) : c.left === 0 ? (
          'Over the threshold by days'
        ) : (
          <>
            <Num>{c.left}</Num> more days make it
          </>
        )}
        {c.reached_on && <> · reached on {shortDay(c.reached_on, today)} if you stay</>}
        {c.reentry && c.reentry > today && (
          <> · a full stay fits again from {shortDay(c.reentry, today)}</>
        )}
      </span>
      <span className="k-muted">Counted from your crossings. Rule: {c.source || c.name}</span>
    </Row>
  );
}

function years(crossings: TravelCrossing[]): [string, TravelCrossing[]][] {
  const by = new Map<string, TravelCrossing[]>();
  for (const c of crossings) {
    const y = c.date.slice(0, 4);
    by.set(y, [...(by.get(y) ?? []), c]);
  }
  return [...by.entries()];
}

/** Where the owner has been: the counters and the crossings. */
export function TravelDaysPage() {
  const today = todayLocal();
  const counters = useCounters(today);
  const crossings = useCrossings();
  const [editing, setEditing] = useState<TravelCrossing | 'new' | null>(null);
  const remove = useDeleteCrossing();
  useErrorToast(remove.error);
  const here = counters.data?.current_country ?? [];
  const now = whereNow(crossings.data ?? [], here, today);
  return (
    <TravelFrame
      title="Days"
      headline={
        now?.since ? (
          <>
            In {countryName(now.country)} since {shortDay(now.since, today)}
          </>
        ) : here.length ? (
          `In ${here.map(countryName).join(', ')}`
        ) : (
          'Days by country'
        )
      }
      sub="counted from your border crossings"
      actions={
        <>
          <span className="k-grow" />
          <button type="button" className="k-btn k-solid" onClick={() => setEditing('new')}>
            Record a crossing
          </button>
        </>
      }
    >
      {counters.isPending ? (
        <Placeholder />
      ) : counters.isError ? (
        <LoadError error={counters.error} onRetry={() => void counters.refetch()} />
      ) : (
        <Group label="Counters">
          {counters.data.data.length === 0 ? (
            <p className="k-empty">
              No counters yet. <Link to="/travel/rules">Add one</Link>: Schengen 90/180, tax
              residency or a visa limit.
            </p>
          ) : (
            counters.data.data.map((c) => <CounterRow key={c.id} c={c} today={today} />)
          )}
        </Group>
      )}
      {crossings.isError ? (
        <LoadError error={crossings.error} onRetry={() => void crossings.refetch()} />
      ) : (
        years(crossings.data ?? []).map(([year, list]) => (
          <Group key={year} label={`Crossings · ${year}`} total={<Num>{list.length}</Num>}>
            {list.map((c) => (
              <Row
                key={c.id}
                dot="var(--g-brand)"
                title={`${countryName(c.from_country)} → ${countryName(c.to_country)}`}
                sub={[shortDay(c.date, today), c.time, c.place].filter(Boolean).join(' · ')}
              >
                {c.note && <span>{c.note}</span>}
                <span className="k-actions">
                  <button type="button" className="k-link" onClick={() => setEditing(c)}>
                    Edit
                  </button>
                  <button
                    type="button"
                    className="k-link k-error-text"
                    onClick={() => remove.mutate(c.id)}
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
        <CrossingSheet
          crossing={editing === 'new' ? undefined : editing}
          onClose={() => setEditing(null)}
        />
      )}
    </TravelFrame>
  );
}

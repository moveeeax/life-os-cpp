import { useQuery } from '@tanstack/react-query';
import { useState } from 'react';

import { useMoneySettings } from '@/hooks/useMoney';
import { useCounters, useCrossings, useTrip, useTrips } from '@/hooks/useTravel';
import { api } from '@/lib/api/client';
import type { Today } from '@/lib/now/types';
import { CURRENCY_OF } from '@/lib/now/currency';
import { countryName } from '@/lib/travel';

import { hhmm, type NowDay } from '../ribbon/layout';
import { MODE_KEY, arrival, flightsToday, hourIn, minutesUntil, modeOf, type Mode } from './modes';
import './modes.css';

function readPin(): string | null {
  try {
    return localStorage.getItem(MODE_KEY);
  } catch {
    return null;
  }
}

function Card({ k, v, s }: { k: string; v: React.ReactNode; s?: React.ReactNode }) {
  return (
    <div className="m-card">
      <div className="m-k">{k}</div>
      <div className="m-v">{v}</div>
      {s && <div className="m-s">{s}</div>}
    </div>
  );
}

const hm = (minutes: number) =>
  `${Math.floor(minutes / 60)} h ${String(minutes % 60).padStart(2, '0')}`;

/**
 * Now by the situation: on a flight day, the first days in a new country, in
 * the morning and in the evening it shows three cards for that moment.
 * Hide turns them off (Arrange turns them on again).
 */
export function ModeCards({
  now,
  date,
  today,
  day,
  tz,
}: {
  now: Date;
  date: string;
  tz: string;
  today: Today | undefined;
  day: NowDay | undefined;
}) {
  const [pin, setPin] = useState<string | null>(readPin);
  const trips = useTrips();
  const travelling = (trips.data ?? []).find((t) => t.start_date === date || t.end_date === date);
  const trip = useTrip(travelling?.id ?? '', 'USD');
  const flights = travelling ? flightsToday(trip.data, date) : [];
  const crossings = useCrossings();
  const arrived = arrival(crossings.data, date);
  const counters = useCounters(date);
  const view = useMoneySettings().data?.view_currency ?? 'KZT';
  const local = arrived ? CURRENCY_OF[arrived.to_country] : undefined;
  const rate = useQuery({
    queryKey: ['now', 'mode-rate', local, view, date],
    enabled: !!local && local !== view,
    staleTime: 60 * 60_000,
    queryFn: async ({ signal }) =>
      (
        await api.getJson<{ data: { amount: number } }>('/api/v1/money/rates/convert', {
          query: { amount: 1, from: local!, to: view, date },
          signal,
        })
      ).data,
  });
  const waiting = useQuery({
    queryKey: ['telegram', 'waiting'],
    staleTime: 10 * 60_000,
    retry: false,
    queryFn: async ({ signal }) =>
      (await api.getJson<{ data: { title: string }[] }>('/api/v1/telegram/waiting', { signal }))
        .data,
  });

  const mode: Mode | null = modeOf(hourIn(now, tz), flights.length > 0, !!arrived, pin);
  if (!mode) return null;
  const hide = () => {
    try {
      localStorage.setItem(MODE_KEY, 'off');
    } catch {
      // Remembering it is a convenience.
    }
    setPin('off');
  };
  const nowMs = now.getTime();
  const firstEvent = (day?.events ?? []).find((e) => !e.all_day && Date.parse(e.end) > nowMs);
  const at = (iso: string) => hhmm(Date.parse(iso), tz);
  const tasks = today?.tasks?.today ?? [];
  const done = tasks.filter((t) => t.status === 'done').length;
  let cards: React.ReactNode[] = [];
  if (mode === 'flight') {
    const f = flights[0];
    const left = minutesUntil(f?.time, now);
    cards = [
      <Card
        key="f"
        k="Flight"
        v={<span className="num">{f ? `${f.time ?? ''} ${f.title}` : 'Today'}</span>}
        s={
          left !== null && left > 0 ? (
            <span className="num">in {hm(left)}</span>
          ) : (
            f?.reference || undefined
          )
        }
      />,
      <Card
        key="p"
        k="Packing"
        v={
          <span className="num">
            {trip.data?.packing.done ?? 0} of {trip.data?.packing.total ?? 0}
          </span>
        }
        s={travelling?.name}
      />,
      <Card
        key="n"
        k="Next"
        v={
          firstEvent ? (
            <span className="num">
              {at(firstEvent.start)} {firstEvent.title}
            </span>
          ) : (
            'Nothing in the calendar'
          )
        }
      />,
    ];
  } else if (mode === 'arrive' && arrived) {
    const rule = (counters.data?.data ?? []).find((c) => c.countries.includes(arrived.to_country));
    cards = [
      <Card
        key="w"
        k={countryName(arrived.to_country)}
        v={<span className="num">{hhmm(now.getTime(), tz)}</span>}
        s={`since ${arrived.date}${arrived.from_country ? `, from ${countryName(arrived.from_country)}` : ''}`}
      />,
      <Card
        key="m"
        k="Money"
        v={
          local && rate.data ? (
            <span className="num">
              1 {local} ≈ {rate.data.amount.toFixed(rate.data.amount < 1 ? 4 : 2)} {view}
            </span>
          ) : (
            (local ?? '—')
          )
        }
      />,
      <Card
        key="r"
        k="Rules"
        v={
          rule ? (
            <span className="num">
              {rule.used} of {rule.threshold_days} days
            </span>
          ) : (
            'No rule here'
          )
        }
        s={rule?.name}
      />,
    ];
  } else if (mode === 'morning') {
    const sleep = today?.health?.sleep_minutes;
    cards = [
      <Card key="s" k="Sleep" v={sleep ? <span className="num">{hm(sleep)}</span> : '—'} />,
      <Card
        key="e"
        k="First up"
        v={
          firstEvent ? (
            <span className="num">
              {at(firstEvent.start)} {firstEvent.title}
            </span>
          ) : (
            'Nothing in the calendar'
          )
        }
      />,
      <Card
        key="t"
        k="Today"
        v={<span className="num">{tasks.length === 1 ? '1 task' : `${tasks.length} tasks`}</span>}
      />,
    ];
  } else if (mode === 'evening') {
    const owed = Array.isArray(waiting.data) ? waiting.data : [];
    cards = [
      <Card
        key="d"
        k="Today"
        v={
          <span className="num">
            {done} of {tasks.length} tasks
          </span>
        }
        s={
          today?.habits ? (
            <span className="num">
              habits {today.habits.done} of {today.habits.total}
            </span>
          ) : undefined
        }
      />,
      <Card
        key="w"
        k="Waiting"
        v={owed.length > 0 ? owed[0].title : 'Nobody'}
        s={owed.length > 1 ? <span className="num">and {owed.length - 1} more</span> : undefined}
      />,
    ];
  }
  return (
    <div className="m-cards" aria-label="For now">
      {cards}
      <button type="button" className="m-hide" onClick={hide} aria-label="Hide these cards">
        Hide
      </button>
    </div>
  );
}

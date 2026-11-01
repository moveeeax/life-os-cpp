import { useQuery, useQueryClient } from '@tanstack/react-query';
import { useState } from 'react';

import { useMetrics } from '@/hooks/useMetrics';
import { useCreateEntry, useFoodDay, useFoodRecent } from '@/hooks/useFood';
import { useReport } from '@/hooks/useMoney';
import { api, ApiClientError } from '@/lib/api/client';
import { shiftDays } from '@/lib/dates';
import { qk } from '@/lib/api/queryKeys';
import { formatTotal } from '@/lib/networth';
import type { Today } from '@/lib/now/types';

import { hourIn } from '../modes/modes';
import {
  FIXED,
  GLASS_ML,
  mealAt,
  quickFoods,
  sparkPoints,
  trendWords,
  weekRange,
  widgetKey,
  type Widget,
} from './widgets';
import './widgets.css';

/**
 * The API's answer for an account that is not linked: 404 (calendar_not_linked,
 * telegram_not_linked, not configured on this site) or 409 (calendar_disconnected).
 * Any other error (a 429, a 500, offline) says nothing about the link.
 */
function unlinked(err: unknown): boolean {
  return err instanceof ApiClientError && (err.status === 404 || err.status === 409);
}

/** After an error that is not "unlinked", the widget reads again this much later. */
const RETRY_LATER_MS = 60_000;
const retryLater = (q: { state: { status: string; error: unknown } }) =>
  q.state.status === 'error' && !unlinked(q.state.error) ? RETRY_LATER_MS : false;

function seriesNumber(v: number): string {
  return new Intl.NumberFormat('en-US', {
    maximumFractionDigits: Math.abs(v) >= 100 ? 0 : 1,
  }).format(v);
}

function Spark({ values, color }: { values: (number | null)[]; color: string }) {
  const pts = sparkPoints(values);
  if (!pts) return <div className="w-spark-empty" />;
  return (
    <svg className="w-spark" viewBox="0 0 100 34" preserveAspectRatio="none" aria-hidden="true">
      <polyline
        points={pts}
        fill="none"
        stroke={color}
        strokeWidth="1.6"
        vectorEffect="non-scaling-stroke"
      />
    </svg>
  );
}

function Frame({
  k,
  v,
  s,
  children,
}: {
  k: string;
  v: React.ReactNode;
  s?: React.ReactNode;
  children?: React.ReactNode;
}) {
  return (
    <div className="w-card">
      <div className="w-k">{k}</div>
      <div className="w-v">{v}</div>
      {children}
      {s && <div className="w-s">{s}</div>}
    </div>
  );
}

function SeriesWidget({ source, date, tz }: { source: string; date: string; tz: string }) {
  const catalog = useMetrics();
  const entry = catalog.data?.find((m) => m.id === source);
  const series = useQuery({
    queryKey: ['metrics', 'series', source, date, tz],
    staleTime: 10 * 60_000,
    queryFn: async ({ signal }) =>
      (
        await api.getJson<{ data: { date: string; value: number | null }[] }>(
          '/api/v1/metrics/series',
          {
            query: { source, from: shiftDays(date, -13), to: date, tz },
            signal,
          },
        )
      ).data,
  });
  const values = (Array.isArray(series.data) ? series.data : []).map((p) => p.value);
  const last = [...values].reverse().find((v) => v !== null);
  const unit = entry?.unit ?? '';
  return (
    <Frame
      k={entry?.label ?? source}
      v={
        <span className="num">
          {last == null ? '—' : `${seriesNumber(last)}${unit ? ` ${unit}` : ''}`}
        </span>
      }
      s={<span className="num">{trendWords(values, unit)}</span>}
    >
      <Spark values={values} color="#a5b4fc" />
    </Frame>
  );
}

function CalendarWeek({ date, tz }: { date: string; tz: string }) {
  const w = weekRange(date);
  const events = useQuery({
    queryKey: ['calendar', 'week', w.from, tz],
    staleTime: 10 * 60_000,
    retry: false,
    refetchInterval: retryLater,
    queryFn: async ({ signal }) =>
      (
        await api.getJson<{ data: { start: string; all_day: boolean }[] }>(
          '/api/v1/calendar/events',
          {
            query: { from: w.from, to: w.to, tz },
            signal,
          },
        )
      ).data,
  });
  const list = Array.isArray(events.data) ? events.data.filter((e) => !e.all_day) : [];
  const per = w.days.map(
    (d) =>
      list.filter(
        (e) => new Intl.DateTimeFormat('en-CA', { timeZone: tz }).format(new Date(e.start)) === d,
      ).length,
  );
  const max = Math.max(1, ...per);
  return (
    <Frame
      k="Calendar week"
      v={
        events.isError ? (
          unlinked(events.error) ? (
            'Not connected'
          ) : (
            '—'
          )
        ) : (
          <span className="num">{list.length} meetings</span>
        )
      }
    >
      <div className="w-week" aria-hidden="true">
        {per.map((n, i) => (
          <div
            key={w.days[i]}
            className={w.days[i] === date ? 'w-today' : undefined}
            style={{ height: `${4 + (n / max) * 26}px` }}
          />
        ))}
      </div>
    </Frame>
  );
}

function Spend({ date }: { date: string }) {
  const report = useReport('month', date);
  const a = report.data?.as_if;
  return (
    <Frame
      k="Spending this month"
      v={
        a ? (
          <span className="num">
            ≈ {formatTotal(a.expense)} {a.currency}
          </span>
        ) : (
          '—'
        )
      }
      s={a?.partial ? 'some currencies without a rate' : undefined}
    />
  );
}

type Water = { date: string; ml: number; goal_ml: number };

const litres = (ml: number) =>
  new Intl.NumberFormat('en-US', { maximumFractionDigits: 2 }).format(ml / 1000);

/** Water of the day: a glass in one tap, and one back if it was a slip. */
function WaterWidget({ date }: { date: string }) {
  const qc = useQueryClient();
  const water = useQuery({
    queryKey: qk.food.water(date),
    retry: false,
    queryFn: async ({ signal }) =>
      (await api.getJson<{ data: Water }>('/api/v1/food/water', { query: { date }, signal })).data,
  });
  const [busy, setBusy] = useState(false);
  const add = async (ml: number) => {
    setBusy(true);
    try {
      const r = await api.postJson<{ data: Water }>('/api/v1/food/water', {
        body: { date, add_ml: ml },
      });
      qc.setQueryData(qk.food.water(date), r.data);
    } catch {
      // The number stays as it was; the next tap tries again.
    }
    setBusy(false);
  };
  const w = water.data;
  return (
    <Frame
      k={FIXED.water}
      v={
        w ? (
          <span className="num">
            {litres(w.ml)} of {litres(w.goal_ml)} l
          </span>
        ) : (
          '—'
        )
      }
    >
      <div className="w-taps">
        <button type="button" disabled={busy || !w} onClick={() => void add(GLASS_ML)}>
          + {GLASS_ML} ml
        </button>
        {w && w.ml > 0 && (
          <button
            type="button"
            className="w-tap-quiet"
            disabled={busy}
            aria-label={`Take back ${GLASS_ML} ml`}
            onClick={() => void add(-GLASS_ML)}
          >
            −
          </button>
        )}
      </div>
    </Frame>
  );
}

/** Protein of the day against the goal, and the two richest recent products in one tap. */
function ProteinWidget({ date, tz }: { date: string; tz: string }) {
  const day = useFoodDay(date);
  const recent = useFoodRecent();
  const create = useCreateEntry();
  const eaten = day.data?.totals.protein_g;
  const goal = day.data?.targets?.protein_g;
  const quick = quickFoods(recent.data ?? []);
  return (
    <Frame
      k={FIXED.protein}
      v={
        eaten == null ? (
          '—'
        ) : (
          <span className="num">
            {Math.round(eaten)}
            {goal ? ` of ${goal}` : ''} g
          </span>
        )
      }
    >
      {quick.length > 0 && (
        <div className="w-taps">
          {quick.map(({ item, grams }) => (
            <button
              key={item.id}
              type="button"
              disabled={create.isPending}
              title={`${item.name}, ${grams} g`}
              onClick={() =>
                create.mutate({
                  date,
                  meal: mealAt(hourIn(new Date(), tz)),
                  item_id: item.id,
                  grams,
                })
              }
            >
              + {item.name}
            </button>
          ))}
        </div>
      )}
    </Frame>
  );
}

function TelegramWidget() {
  const waiting = useQuery({
    queryKey: qk.telegramWaiting(),
    staleTime: 10 * 60_000,
    retry: false,
    refetchInterval: retryLater,
    queryFn: async ({ signal }) =>
      (await api.getJson<{ data: { title: string }[] }>('/api/v1/telegram/waiting', { signal }))
        .data,
  });
  const list = Array.isArray(waiting.data) ? waiting.data : [];
  return (
    <Frame
      k="Telegram"
      v={
        waiting.isError ? (
          unlinked(waiting.error) ? (
            'Not linked'
          ) : (
            '—'
          )
        ) : list.length === 0 ? (
          'Nobody waits'
        ) : (
          <span className="num">{list.length} waiting</span>
        )
      }
      s={list[0] ? `${list[0].title} waits for your answer` : undefined}
    />
  );
}

/** The widget board of Now: what the person put on it in Arrange, in order. */
export function Board({
  widgets,
  today,
  date,
  tz,
}: {
  widgets: Widget[];
  today: Today | undefined;
  date: string;
  tz: string;
}) {
  if (widgets.length === 0) return null;
  return (
    <div className="w-board" aria-label="Widgets">
      {widgets.map((w) => {
        const key = widgetKey(w);
        if (w.kind === 'series' && w.source)
          return <SeriesWidget key={key} source={w.source} date={date} tz={tz} />;
        if (w.kind === 'calendar_week') return <CalendarWeek key={key} date={date} tz={tz} />;
        if (w.kind === 'spend') return <Spend key={key} date={date} />;
        if (w.kind === 'telegram') return <TelegramWidget key={key} />;
        if (w.kind === 'water') return <WaterWidget key={key} date={date} />;
        if (w.kind === 'protein') return <ProteinWidget key={key} date={date} tz={tz} />;
        if (w.kind === 'habits') {
          const h = today?.habits;
          return (
            <Frame
              key={key}
              k={FIXED.habits}
              v={
                h ? (
                  <span className="num">
                    {h.done} of {h.total}
                  </span>
                ) : (
                  '—'
                )
              }
              s={h && h.left.length > 0 ? `left: ${h.left.slice(0, 3).join(', ')}` : undefined}
            />
          );
        }
        if (w.kind === 'trip') {
          const t = today?.travel?.next_trip;
          return (
            <Frame
              key={key}
              k={FIXED.trip}
              v={
                t ? (
                  <span className="num">{t.days_to <= 0 ? 'now' : `in ${t.days_to} days`}</span>
                ) : (
                  'None planned'
                )
              }
              s={t?.name}
            />
          );
        }
        return null;
      })}
    </div>
  );
}

import { useEffect, useState } from 'react';
import { ShieldAlert } from 'lucide-react';
import { useSearchParams } from 'react-router';

import {
  useActivity,
  useBody,
  useHeartRate,
  useSleep,
  useSpo2,
  useStress,
  useSummary,
  useWorkouts,
} from '@/hooks/useHealth';
import { useMe } from '@/hooks/useMe';
import { Permission, userCan } from '@/lib/auth/permissions';
import {
  MAX_RANGE_DAYS,
  MIN_DATE,
  PRESETS,
  alignToDays,
  dailyStats,
  daysInRange,
  formatDelta,
  formatMinutes,
  inRange,
  localDate,
  mean,
  nights,
  parseRange,
  today,
  validCustomRange,
  weightChange,
  type DateRange,
  type DayStat,
} from '@/lib/health';
import { cn } from '@/lib/utils';

import { ChartCard, cardClass } from './health/ChartCard';
import { SyncPanel } from './health/SyncPanel';
import { TimeChart } from './health/TimeChart';

const C = {
  brand: '#465fff',
  green: '#12b76a',
  orange: '#fb6514',
  violet: '#7a5af8',
  sky: '#0ba5ec',
  gray: '#98a2b3',
  red: '#f04438',
};

const statSeries = (days: string[], stats: DayStat[], pick: (s: DayStat) => number) =>
  alignToDays(
    days,
    stats,
    (s) => s.date,
    (s) => Math.round(pick(s) * 10) / 10,
  );

function Tile({ label, value, note }: { label: string; value: string; note: string }) {
  return (
    <div className={cardClass}>
      <p className="text-theme-sm text-gray-500 dark:text-gray-400">{label}</p>
      <p className="mt-2 text-title-sm font-semibold text-gray-800 dark:text-white/90">{value}</p>
      <p className="mt-1 text-theme-xs text-gray-500 dark:text-gray-400">{note}</p>
    </div>
  );
}

const days_ = (n: number) => `${n} ${n === 1 ? 'day' : 'days'}`;

function Tiles({ range }: { range: DateRange }) {
  const summary = useSummary(range);
  const body = useBody(range);
  const rows = summary.data ?? [];

  const steps = mean(rows.map((r) => r.steps));
  const sleep = mean(rows.map((r) => r.sleep_duration_minutes));
  const resting = mean(rows.map((r) => r.resting_bpm));
  const weight = weightChange(
    (body.data ?? []).filter((r) => inRange(localDate(r.timestamp), range)),
  );

  const pending = '…';
  const failed = 'unavailable';
  const sNote = summary.isPending ? pending : summary.error ? failed : null;
  const bNote = body.isPending ? pending : body.error ? failed : null;

  return (
    <div className="grid grid-cols-1 gap-4 sm:grid-cols-2 xl:grid-cols-4">
      <Tile
        label="Steps per day"
        value={steps.value === null ? '–' : Math.round(steps.value).toLocaleString('en-US')}
        note={sNote ?? `mean of ${days_(steps.n)}`}
      />
      <Tile
        label="Sleep per night"
        value={sleep.value === null ? '–' : formatMinutes(sleep.value)}
        note={sNote ?? `mean of ${days_(sleep.n)}, h:mm`}
      />
      <Tile
        label="Resting heart rate"
        value={resting.value === null ? '–' : `${Math.round(resting.value)} bpm`}
        note={sNote ?? `mean of ${days_(resting.n)}`}
      />
      <Tile
        label="Weight"
        value={weight.last === null ? '–' : `${weight.last.toFixed(1)} kg`}
        note={
          bNote ??
          (weight.delta === null
            ? `${weight.n} ${weight.n === 1 ? 'measurement' : 'measurements'}`
            : `${formatDelta(weight.delta)} kg over ${weight.n} measurements`)
        }
      />
    </div>
  );
}

function Charts({ range }: { range: DateRange }) {
  const days = daysInRange(range);
  const activity = useActivity(range);
  const sleep = useSleep(range);
  const heart = useHeartRate(range);
  const stress = useStress(range);
  const body = useBody(range);
  const summary = useSummary(range);

  const act = activity.data ?? [];
  const night = nights(sleep.data ?? []).filter((n) => inRange(n.date, range));
  const hr = dailyStats(
    (heart.data ?? []).map((s) => ({ timestamp: s.timestamp, value: s.bpm })),
  ).filter((s) => inRange(s.date, range));
  const st = dailyStats(
    (stress.data ?? []).map((s) => ({ timestamp: s.timestamp, value: s.stress_score })),
  ).filter((s) => inRange(s.date, range));
  // Several weigh-ins on one day: the last one of the day stands for it.
  const weighIns = (body.data ?? []).filter((r) => inRange(localDate(r.timestamp), range));

  return (
    <div className="grid grid-cols-1 gap-4 xl:grid-cols-2">
      <ChartCard
        title="Activity"
        hint="Steps per day and active calories."
        isPending={activity.isPending}
        error={activity.error}
        onRetry={() => activity.refetch()}
        isEmpty={act.length === 0}
      >
        <TimeChart
          days={days}
          yBounds={[{ min: 0 }, { min: 0 }]}
          yTitles={['steps', 'kcal']}
          series={[
            {
              name: 'Steps',
              type: 'column',
              color: C.brand,
              data: alignToDays(
                days,
                act,
                (r) => r.date,
                (r) => r.steps,
              ),
            },
            {
              name: 'Active kcal',
              type: 'line',
              axis: 1,
              color: C.orange,
              data: alignToDays(
                days,
                act,
                (r) => r.date,
                (r) => r.active_kcal,
              ),
            },
          ]}
        />
      </ChartCard>

      <ChartCard
        title="Sleep"
        hint="Minutes per stage for the longest session of each night, and the sleep score."
        isPending={sleep.isPending}
        error={sleep.error}
        onRetry={() => sleep.refetch()}
        isEmpty={night.length === 0}
      >
        <TimeChart
          days={days}
          yBounds={[{ min: 0 }, { min: 0, max: 100 }]}
          stacked
          yTitles={['minutes', 'score']}
          format={(v, i) => (i < 4 ? formatMinutes(v) : String(v))}
          series={[
            {
              name: 'Deep',
              type: 'column',
              color: C.brand,
              data: alignToDays(
                days,
                night,
                (n) => n.date,
                (n) => n.deep,
              ),
            },
            {
              name: 'Light',
              type: 'column',
              color: C.sky,
              data: alignToDays(
                days,
                night,
                (n) => n.date,
                (n) => n.light,
              ),
            },
            {
              name: 'REM',
              type: 'column',
              color: C.violet,
              data: alignToDays(
                days,
                night,
                (n) => n.date,
                (n) => n.rem,
              ),
            },
            {
              name: 'Awake',
              type: 'column',
              color: C.gray,
              data: alignToDays(
                days,
                night,
                (n) => n.date,
                (n) => n.awake,
              ),
            },
            {
              name: 'Score',
              type: 'line',
              axis: 1,
              color: C.green,
              data: alignToDays(
                days,
                night,
                (n) => n.date,
                (n) => n.score,
              ),
            },
          ]}
        />
      </ChartCard>

      <ChartCard
        title="Heart rate"
        hint="Lowest, mean and highest sample of each day, and the resting rate."
        isPending={heart.isPending}
        error={heart.error}
        onRetry={() => heart.refetch()}
        isEmpty={hr.length === 0}
      >
        <TimeChart
          days={days}
          yTitles={['bpm']}
          series={[
            { name: 'Max', type: 'line', color: C.red, data: statSeries(days, hr, (s) => s.max) },
            {
              name: 'Mean',
              type: 'line',
              color: C.brand,
              data: statSeries(days, hr, (s) => s.avg),
            },
            { name: 'Min', type: 'line', color: C.sky, data: statSeries(days, hr, (s) => s.min) },
            {
              name: 'Resting',
              type: 'line',
              color: C.green,
              data: alignToDays(
                days,
                summary.data ?? [],
                (r) => r.date,
                (r) => r.resting_bpm,
              ),
            },
          ]}
        />
      </ChartCard>

      <ChartCard
        title="Stress"
        hint="Mean and highest stress score of each day."
        isPending={stress.isPending}
        error={stress.error}
        onRetry={() => stress.refetch()}
        isEmpty={st.length === 0}
      >
        <TimeChart
          days={days}
          yBounds={[{ min: 0, max: 100 }]}
          yTitles={['score']}
          series={[
            {
              name: 'Mean',
              type: 'area',
              color: C.brand,
              data: statSeries(days, st, (s) => s.avg),
            },
            {
              name: 'Max',
              type: 'line',
              color: C.orange,
              data: statSeries(days, st, (s) => s.max),
            },
          ]}
        />
      </ChartCard>

      <div className="xl:col-span-2">
        <ChartCard
          title="Body"
          hint="Weight, body fat and muscle mass; the last weigh-in of each day."
          isPending={body.isPending}
          error={body.error}
          onRetry={() => body.refetch()}
          isEmpty={weighIns.length === 0}
        >
          <TimeChart
            days={days}
            connect
            yTitles={['kg', '%']}
            series={[
              {
                name: 'Weight, kg',
                type: 'line',
                color: C.brand,
                data: alignToDays(
                  days,
                  weighIns,
                  (r) => localDate(r.timestamp),
                  (r) => r.weight_kg,
                ),
              },
              {
                name: 'Muscle, kg',
                type: 'line',
                color: C.green,
                data: alignToDays(
                  days,
                  weighIns,
                  (r) => localDate(r.timestamp),
                  (r) => r.muscle_mass_kg,
                ),
              },
              {
                name: 'Body fat, %',
                type: 'line',
                axis: 1,
                color: C.orange,
                data: alignToDays(
                  days,
                  weighIns,
                  (r) => localDate(r.timestamp),
                  (r) => r.body_fat_pct,
                ),
              },
            ]}
          />
        </ChartCard>
      </div>
    </div>
  );
}

const cell = 'py-2 pe-4';
const headCell = 'py-2 pe-4 font-medium';
const headRow =
  'border-b border-gray-200 text-theme-xs text-gray-500 uppercase dark:border-gray-800 dark:text-gray-400';
const bodyRow = 'border-b border-gray-100 last:border-0 dark:border-gray-800';

function Tables({ range }: { range: DateRange }) {
  const spo2 = useSpo2(range);
  const workouts = useWorkouts(range);
  const spo2Rows = (spo2.data ?? []).filter((r) => inRange(localDate(r.timestamp), range));
  const workoutRows = (workouts.data ?? []).filter((r) => inRange(localDate(r.start_at), range));

  return (
    <div className="grid grid-cols-1 gap-4 xl:grid-cols-2">
      <ChartCard
        title="SpO2"
        isPending={spo2.isPending}
        error={spo2.error}
        onRetry={() => spo2.refetch()}
        isEmpty={spo2Rows.length === 0}
        emptyText="No SpO2 measurements in this period."
      >
        <div className="max-h-64 overflow-auto">
          <table className="w-full text-left text-theme-sm">
            <thead>
              <tr className={headRow}>
                <th scope="col" className={headCell}>
                  Time
                </th>
                <th scope="col" className={cn(headCell, 'text-right')}>
                  SpO2, %
                </th>
              </tr>
            </thead>
            <tbody>
              {spo2Rows.map((r) => (
                <tr key={r.timestamp} className={bodyRow}>
                  <td className={cn(cell, 'text-gray-500 dark:text-gray-400')}>
                    {new Date(r.timestamp).toLocaleString()}
                  </td>
                  <td className={cn(cell, 'text-right tabular-nums')}>{r.spo2_pct}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      </ChartCard>

      <ChartCard
        title="Workouts"
        isPending={workouts.isPending}
        error={workouts.error}
        onRetry={() => workouts.refetch()}
        isEmpty={workoutRows.length === 0}
        emptyText="No workouts in this period."
      >
        <div className="max-h-64 overflow-auto">
          <table className="w-full text-left text-theme-sm">
            <thead>
              <tr className={headRow}>
                <th scope="col" className={headCell}>
                  Start
                </th>
                <th scope="col" className={headCell}>
                  Type
                </th>
                <th scope="col" className={cn(headCell, 'text-right')}>
                  Minutes
                </th>
                <th scope="col" className={cn(headCell, 'text-right')}>
                  km
                </th>
                <th scope="col" className={cn(headCell, 'text-right')}>
                  kcal
                </th>
              </tr>
            </thead>
            <tbody>
              {workoutRows.map((r) => (
                <tr key={r.workout_id} className={bodyRow}>
                  <td className={cn(cell, 'text-gray-500 dark:text-gray-400')}>
                    {new Date(r.start_at).toLocaleString()}
                  </td>
                  <td className={cell}>{r.activity_type}</td>
                  <td className={cn(cell, 'text-right tabular-nums')}>
                    {r.duration_minutes ?? '–'}
                  </td>
                  <td className={cn(cell, 'text-right tabular-nums')}>
                    {r.distance_m === null ? '–' : (r.distance_m / 1000).toFixed(2)}
                  </td>
                  <td className={cn(cell, 'text-right tabular-nums')}>{r.calories_kcal ?? '–'}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      </ChartCard>
    </div>
  );
}

const dateInput =
  'h-10 rounded-lg border border-gray-300 bg-transparent px-3 text-sm text-gray-800 shadow-theme-xs focus:border-brand-300 focus:ring-3 focus:ring-brand-500/20 focus:outline-hidden dark:border-gray-700 dark:bg-gray-900 dark:text-white/90';

function PeriodPicker() {
  const [params, setParams] = useSearchParams();
  const end = today();
  const range = parseRange(params, end);

  // The date inputs edit a draft. A date input reports every keystroke (a
  // half-typed year arrives as 0002), so the range is applied only on blur or
  // Enter, and only when it is inside the limits.
  const [draft, setDraft] = useState({ from: range.from, to: range.to });
  const [rejected, setRejected] = useState(false);
  useEffect(() => {
    setDraft({ from: range.from, to: range.to });
    setRejected(false);
  }, [range.from, range.to]);

  const commit = () => {
    if (draft.from === range.from && draft.to === range.to) return;
    if (validCustomRange(draft.from, draft.to, end)) {
      setParams({ from: draft.from, to: draft.to }, { replace: true });
    } else {
      setRejected(true);
    }
  };
  const onKeyDown = (e: React.KeyboardEvent) => {
    if (e.key === 'Enter') commit();
  };

  return (
    <div className="flex flex-col items-end gap-1">
      <div className="flex flex-wrap items-center gap-3">
        <div
          className="inline-flex rounded-lg border border-gray-200 bg-white p-0.5 dark:border-gray-800 dark:bg-white/3"
          role="group"
          aria-label="Period"
        >
          {PRESETS.map((p) => (
            <button
              key={p}
              type="button"
              aria-pressed={range.preset === p}
              onClick={() => setParams({ days: String(p) }, { replace: true })}
              className={cn(
                'rounded-md px-3 py-2 text-theme-sm font-medium',
                range.preset === p
                  ? 'bg-brand-500 text-white'
                  : 'text-gray-500 hover:text-gray-800 dark:text-gray-400 dark:hover:text-white',
              )}
            >
              {p} days
            </button>
          ))}
        </div>
        <div className="flex items-center gap-2">
          <input
            type="date"
            aria-label="From"
            aria-invalid={rejected}
            value={draft.from}
            min={MIN_DATE}
            max={draft.to || end}
            onChange={(e) => setDraft((d) => ({ ...d, from: e.target.value }))}
            onBlur={commit}
            onKeyDown={onKeyDown}
            className={dateInput}
          />
          <span className="text-gray-400">–</span>
          <input
            type="date"
            aria-label="To"
            aria-invalid={rejected}
            value={draft.to}
            min={draft.from || MIN_DATE}
            max={end}
            onChange={(e) => setDraft((d) => ({ ...d, to: e.target.value }))}
            onBlur={commit}
            onKeyDown={onKeyDown}
            className={dateInput}
          />
        </div>
      </div>
      {rejected && (
        <p role="alert" className="text-theme-xs text-error-500">
          Pick a range of at most {MAX_RANGE_DAYS} days, from {MIN_DATE} to today.
        </p>
      )}
    </div>
  );
}

/**
 * Health section: Mi Fitness charts and the sync controls. The permission
 * is checked here, not by a route guard: the guard's fallback is `/`, which
 * sends a signed-in user straight back.
 */
export function HealthPage() {
  const user = useMe().data ?? null;
  const [params] = useSearchParams();

  if (!userCan(user, Permission.FitnessRead)) {
    return (
      <div className={cardClass} role="alert">
        <div className="flex items-start gap-3">
          <ShieldAlert className="size-6 shrink-0 text-error-500" aria-hidden="true" />
          <div>
            <h1 className="text-theme-xl font-semibold text-gray-800 dark:text-white/90">
              No access to Health
            </h1>
            <p className="mt-1 text-sm text-gray-500 dark:text-gray-400">
              Your role does not include reading fitness data. Ask an administrator for the Fitness
              Reader role.
            </p>
          </div>
        </div>
      </div>
    );
  }

  const { from, to } = parseRange(params, today());
  const range = { from, to };

  return (
    <div className="flex flex-col gap-4 md:gap-6">
      <div className="flex flex-wrap items-center justify-between gap-3">
        <h1 className="text-title-sm font-semibold text-gray-800 dark:text-white/90">Health</h1>
        <PeriodPicker />
      </div>
      <Tiles range={range} />
      <Charts range={range} />
      <Tables range={range} />
      {userCan(user, Permission.FitnessSync) && <SyncPanel />}
    </div>
  );
}

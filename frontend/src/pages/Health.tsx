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
import { LinkMiNotice } from '@/components/LinkMiNotice';
import { apiErrorMessage } from '@/lib/api/client';
import { useMe } from '@/hooks/useMe';
import { useMiAccount } from '@/hooks/useMiAccount';
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
  rangeHeadline,
  today,
  validCustomRange,
  weightChange,
  type DateRange,
  type DayStat,
} from '@/lib/health';
import { needsLink } from '@/lib/mi';

import { ChartCard } from './health/ChartCard';
import { SyncPanel } from './health/SyncPanel';
import { Banner, Facts, Group, Num, Row, SectionFrame } from '@/shell/kit';
import { TimeChart } from './health/LazyTimeChart';

// Series colours on the dark glass sheet.
const C = {
  brand: '#a5b4fc',
  green: '#6ee7b7',
  orange: '#fdba74',
  violet: '#c4b5fd',
  sky: '#7dd3fc',
  gray: 'rgba(255, 255, 255, 0.45)',
  red: '#fca5a5',
};

const statSeries = (days: string[], stats: DayStat[], pick: (s: DayStat) => number) =>
  alignToDays(
    days,
    stats,
    (s) => s.date,
    (s) => Math.round(pick(s) * 10) / 10,
  );

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
    <>
      <Facts
        items={[
          {
            label: 'Steps a day',
            value: (
              <span>
                <Num>
                  {steps.value === null ? '–' : Math.round(steps.value).toLocaleString('en-US')}
                </Num>
              </span>
            ),
          },
          {
            label: 'Sleep',
            value: (
              <span>
                <Num>{sleep.value === null ? '–' : formatMinutes(sleep.value)}</Num>
              </span>
            ),
          },
          {
            label: 'Resting',
            value: (
              <span>
                <Num>{resting.value === null ? '–' : `${Math.round(resting.value)} bpm`}</Num>
              </span>
            ),
          },
          {
            label: 'Weight',
            value: (
              <span>
                <Num>
                  {weight.last === null ? '–' : `${weight.last.toFixed(1)} kg`}
                  {weight.delta !== null && `, ${formatDelta(weight.delta)}`}
                </Num>
              </span>
            ),
          },
        ]}
      />
      <p className="k-sub">
        {sNote ?? `Means of ${days_(Math.max(steps.n, sleep.n, resting.n))} with data`}
        {' · weight '}
        {bNote ?? (
          <>
            <span className="num">{weight.n}</span> {weight.n === 1 ? 'weigh-in' : 'weigh-ins'},
            change over the period
          </>
        )}
      </p>
    </>
  );
}

/** Chart rows open on demand, at most two at a time: a third closes the oldest. */
function useOpenCharts(
  max = 2,
): [(key: string) => boolean, (key: string) => (open: boolean) => void] {
  const [open, setOpen] = useState<string[]>([]);
  const isOpen = (key: string) => open.includes(key);
  const toggle = (key: string) => (next: boolean) =>
    setOpen((list) =>
      next ? [...list.filter((k) => k !== key), key].slice(-max) : list.filter((k) => k !== key),
    );
  return [isOpen, toggle];
}

function Charts({ range }: { range: DateRange }) {
  const [isOpen, toggle] = useOpenCharts();
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
    <>
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
        open={isOpen('Sleep')}
        onToggle={toggle('Sleep')}
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
        open={isOpen('Heart rate')}
        onToggle={toggle('Heart rate')}
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
        open={isOpen('Stress')}
        onToggle={toggle('Stress')}
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

      <div>
        <ChartCard
          title="Weight and body fat"
          open={isOpen('Body')}
          onToggle={toggle('Body')}
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
    </>
  );
}

function Tables({ range }: { range: DateRange }) {
  const spo2 = useSpo2(range);
  const workouts = useWorkouts(range);
  const spo2Rows = (spo2.data ?? []).filter((r) => inRange(localDate(r.timestamp), range));
  const workoutRows = (workouts.data ?? []).filter((r) => inRange(localDate(r.start_at), range));
  const when = (iso: string) =>
    new Date(iso).toLocaleString('en-US', {
      month: 'short',
      day: 'numeric',
      hour: '2-digit',
      minute: '2-digit',
    });

  return (
    <>
      <ChartCard
        title="Workouts"
        isPending={workouts.isPending}
        error={workouts.error}
        onRetry={() => workouts.refetch()}
        isEmpty={workoutRows.length === 0}
        emptyText="No workouts in this period."
      >
        {workoutRows.map((r) => (
          <Row
            key={r.workout_id}
            title={<span className="k-cap">{r.activity_type}</span>}
            sub={
              <>
                {when(r.start_at)}
                {r.duration_minutes !== null && (
                  <>
                    {' '}
                    · <Num>{r.duration_minutes} min</Num>
                  </>
                )}
              </>
            }
            right={
              r.distance_m !== null ? (
                <Num>{(r.distance_m / 1000).toFixed(2)} km</Num>
              ) : r.calories_kcal !== null ? (
                <Num>{r.calories_kcal} kcal</Num>
              ) : undefined
            }
          />
        ))}
      </ChartCard>
      <Group label="Band">
        <Row
          title={<span data-num-ok>SpO2</span>}
          sub="readings of the period, newest first"
          right={spo2Rows.length ? <Num>{spo2Rows[spo2Rows.length - 1].spo2_pct}%</Num> : undefined}
        >
          {spo2.isPending ? (
            <div className="k-placeholder" />
          ) : spo2.error ? (
            <p className="k-error">{apiErrorMessage(spo2.error, 'Failed to load.')}</p>
          ) : spo2Rows.length === 0 ? (
            <span>No SpO2 measurements in this period.</span>
          ) : (
            <div className="k-scroll">
              {[...spo2Rows].reverse().map((r) => (
                <div key={r.timestamp} className="k-kv">
                  <span className="k-muted">{when(r.timestamp)}</span>
                  <Num>{r.spo2_pct}%</Num>
                </div>
              ))}
            </div>
          )}
        </Row>
      </Group>
    </>
  );
}

const dateInput = 'k-input k-compact';

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
    <div className="k-period">
      <div className="k-tools k-tools-tight">
        <div className="k-seg" role="group" aria-label="Period">
          {PRESETS.map((p) => (
            <button
              key={p}
              type="button"
              aria-pressed={range.preset === p}
              onClick={() => setParams({ days: String(p) }, { replace: true })}
              className={range.preset === p ? 'k-seg-on' : undefined}
            >
              {`${p} days`}
            </button>
          ))}
        </div>
        <div className="k-dates">
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
          <span className="k-muted">–</span>
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
        <p role="alert" className="k-error" data-num-ok>
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
  const canRead = userCan(user, Permission.FitnessRead);
  // The link status needs fitness:sync; without it the page shows what there is.
  const mi = useMiAccount(canRead && userCan(user, Permission.FitnessSync));

  if (!canRead) {
    return (
      <SectionFrame eyebrow="Body" headline="No access to Health">
        <div className="k-banner" role="alert">
          <span>
            <ShieldAlert className="k-inline-icon" aria-hidden="true" /> Your role does not include
            reading fitness data. Ask an administrator for the Fitness Reader role.
          </span>
        </div>
      </SectionFrame>
    );
  }

  const parsed = parseRange(params, today());
  const { from, to } = parsed;
  const range = { from, to };
  const headline = rangeHeadline(parsed);

  // Nothing is linked: there is no data to chart, only the way to get some.
  if (needsLink(mi.data)) {
    return (
      <SectionFrame eyebrow="Body" headline="Body">
        <LinkMiNotice purpose="to see your steps, sleep, heart rate and weight here" />
      </SectionFrame>
    );
  }

  return (
    <SectionFrame eyebrow="Body" headline={headline} sub="from the band" tools={<PeriodPicker />}>
      {mi.data?.status === 'reauth_required' && (
        <Banner to="/account" action="Link again in profile">
          Xiaomi asks you to sign in again. Syncing is paused; the data below is what was synced
          before.
        </Banner>
      )}
      <Tiles range={range} />
      <Charts range={range} />
      <Tables range={range} />
      {userCan(user, Permission.FitnessSync) && <SyncPanel />}
    </SectionFrame>
  );
}

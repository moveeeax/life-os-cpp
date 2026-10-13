import { Link, useNavigate } from 'react-router';

import { LinkMiNotice } from '@/components/LinkMiNotice';
import { useMe } from '@/hooks/useMe';
import { useMiAccount } from '@/hooks/useMiAccount';
import { useRoutines } from '@/hooks/useWorkout';
import {
  useActiveSession,
  useReadiness,
  useSessions,
  useStartSession,
} from '@/hooks/useWorkoutSession';
import { apiErrorMessage } from '@/lib/api/client';
import { Permission, userCan } from '@/lib/auth/permissions';
import { formatMinutes } from '@/lib/health';
import { needsLink } from '@/lib/mi';
import { weekdayLabel } from '@/lib/workout';
import { formatDay, formatDuration, formatTime, formatVolume } from '@/lib/workout/format';
import { durationMinutes, weekdayOf, workSetCount } from '@/lib/workout/session';
import type { RoutineSummary } from '@/lib/workout/types';

import { HealthBadge, Stat } from './bits';
import { LoadError, WorkoutFrame } from './frame';
import { Banner, Group, Num, Row } from '@/shell/kit';
import { primaryButton, secondaryButton } from '@/shell/kit/styles';

/** Numbers to look at before training; no score and no advice. */
function Readiness() {
  const user = useMe().data ?? null;
  const mi = useMiAccount(userCan(user, Permission.FitnessSync));
  const q = useReadiness();
  const r = q.data;
  const versus = (mean: number | null | undefined, format: (n: number) => string) =>
    mean == null ? 'no 30-day mean' : `30-day mean ${format(mean)}`;

  return (
    <Group label="Before the workout">
      {needsLink(mi.data) ? (
        // These numbers come from the band; logging a workout does not need it.
        <div>
          <LinkMiNotice
            purpose="to see last night's sleep, resting heart rate and stress"
            compact
          />
        </div>
      ) : q.isPending ? (
        <div className="k-placeholder" />
      ) : q.error || !r ? (
        <p className="k-error" role="alert">
          {apiErrorMessage(q.error, 'Failed to load.')}{' '}
          <button type="button" className="k-link" onClick={() => q.refetch()}>
            Retry
          </button>
        </p>
      ) : (
        <div className="k-facts">
          <Stat
            label="Last night's sleep"
            value={r.sleep_minutes == null ? '–' : formatMinutes(r.sleep_minutes)}
            note={[
              r.sleep_score == null ? null : `score ${r.sleep_score}`,
              versus(r.sleep_minutes_avg_30d, formatMinutes),
            ]
              .filter(Boolean)
              .join(' · ')}
          />
          <Stat
            label="Resting heart rate"
            value={r.resting_bpm == null ? '–' : `${r.resting_bpm} bpm`}
            note={versus(r.resting_bpm_avg_30d, (n) => `${n} bpm`)}
          />
          <Stat
            label="Stress, last 12 h"
            value={r.stress == null ? '–' : String(r.stress)}
            note={versus(r.stress_avg_30d, String)}
          />
          <Stat
            label="Body weight"
            value={r.bodyweight_kg == null ? '–' : `${r.bodyweight_kg.toFixed(1)} kg`}
            note="latest weigh-in"
          />
        </div>
      )}
    </Group>
  );
}

function Recent() {
  const q = useSessions(5);
  const rows = q.data?.pages[0]?.data ?? [];
  return (
    <Group
      label="Recent workouts"
      total={
        <Link to="/workout/history" className="k-link">
          All history
        </Link>
      }
    >
      {q.isPending ? (
        <div className="k-placeholder" />
      ) : q.error ? (
        <p className="k-error" role="alert">
          {apiErrorMessage(q.error, 'Failed to load.')}
        </p>
      ) : rows.length === 0 ? (
        <p className="k-empty">No finished workouts yet.</p>
      ) : (
        rows.map((s) => (
          <Row
            key={s.id}
            title={
              <Link to={`/workout/history/${s.id}`} className="k-link-plain">
                {s.name || 'Workout'}
              </Link>
            }
            sub={
              <>
                {formatDay(s.started_at)} ·{' '}
                {formatDuration(durationMinutes(s.started_at, s.finished_at))} ·{' '}
                <Num>{s.set_count} sets</Num> · <HealthBadge status={s.health_status} />
              </>
            }
            right={<Num>{formatVolume(s.volume_kg)}</Num>}
          />
        ))
      )}
    </Group>
  );
}

/** Workout overview: continue or start a workout, readiness, recent sessions. */
export function WorkoutHomePage() {
  const navigate = useNavigate();
  const active = useActiveSession();
  const routines = useRoutines();
  const start = useStartSession(() => navigate('/workout/session'));

  const today = weekdayOf(new Date());
  const all = routines.data ?? [];
  // Today's routines first, then the rest in their own order.
  const ordered = [
    ...all.filter((r) => r.weekday === today),
    ...all.filter((r) => r.weekday !== today),
  ];

  const routineRow = (r: RoutineSummary) => (
    <Row
      key={r.id}
      dot={r.weekday === today ? 'var(--g-ok)' : undefined}
      title={
        <>
          {r.name}
          {r.weekday === today && <span className="k-muted"> · today</span>}
        </>
      }
      sub={
        <>
          {weekdayLabel(r.weekday)} · <Num>{r.exercise_count}</Num>{' '}
          {r.exercise_count === 1 ? 'exercise' : 'exercises'}
        </>
      }
      right={
        <button
          type="button"
          onClick={() => start.mutate(r.id)}
          disabled={start.isPending}
          className={r.weekday === today ? primaryButton : secondaryButton}
        >
          Start
        </button>
      }
    />
  );

  return (
    <WorkoutFrame
      title="Training"
      headline={active.data ? `${active.data.name || 'Workout'} is in progress` : 'Training'}
      actions={
        !active.data && !active.isPending ? (
          <>
            <span className="k-grow" />
            <button
              type="button"
              onClick={() => start.mutate(null)}
              disabled={start.isPending}
              className={secondaryButton}
            >
              Empty workout
            </button>
          </>
        ) : undefined
      }
    >
      {active.isPending || routines.isPending ? (
        <div className="k-placeholder" aria-label="Loading" />
      ) : active.error ? (
        <LoadError error={active.error} onRetry={() => active.refetch()} />
      ) : active.data ? (
        <Banner to="/workout/session" action="Continue">
          Started {formatDay(active.data.started_at)} at {formatTime(active.data.started_at)} ·{' '}
          <Num>{workSetCount(active.data)}</Num> work sets logged
        </Banner>
      ) : (
        <Group label="Start a workout">
          {routines.error ? (
            <p className="k-error" role="alert">
              {apiErrorMessage(routines.error, 'Failed to load routines.')}
            </p>
          ) : ordered.length === 0 ? (
            <p className="k-empty">
              No routines yet.{' '}
              <Link to="/workout/routines/new" className="k-link">
                Create one
              </Link>{' '}
              or start an empty workout and add exercises as you go.
            </p>
          ) : (
            ordered.map(routineRow)
          )}
          {start.error && (
            <p role="alert" className="k-error">
              {String(start.error)}
            </p>
          )}
        </Group>
      )}
      <Readiness />
      <Recent />
    </WorkoutFrame>
  );
}

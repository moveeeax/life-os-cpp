import { Play } from 'lucide-react';
import { Link, useNavigate } from 'react-router';

import { useRoutines } from '@/hooks/useWorkout';
import {
  useActiveSession,
  useReadiness,
  useSessions,
  useStartSession,
} from '@/hooks/useWorkoutSession';
import { apiErrorMessage } from '@/lib/api/client';
import { formatMinutes } from '@/lib/health';
import { weekdayLabel } from '@/lib/workout';
import { formatDay, formatDuration, formatTime, formatVolume } from '@/lib/workout/format';
import { durationMinutes, weekdayOf, workSetCount } from '@/lib/workout/session';
import type { RoutineSummary } from '@/lib/workout/types';
import { cn } from '@/lib/utils';

import { HealthBadge, Stat } from './bits';
import { LoadError, WorkoutFrame } from './frame';
import { cardClass, primaryButton, secondaryButton } from './styles';

const heading = 'text-lg font-semibold text-gray-800 dark:text-white/90';
const muted = 'text-theme-sm text-gray-500 dark:text-gray-400';

/** Numbers to look at before training; no score and no advice. */
function Readiness() {
  const q = useReadiness();
  const r = q.data;
  const versus = (mean: number | null | undefined, format: (n: number) => string) =>
    mean == null ? 'no 30-day mean' : `30-day mean ${format(mean)}`;

  return (
    <section className={cardClass} aria-label="Readiness">
      <h2 className={heading}>Before the workout</h2>
      {q.isPending ? (
        <div className="mt-4 h-16 animate-pulse rounded-xl bg-gray-100 dark:bg-white/5" />
      ) : q.error || !r ? (
        <p className="mt-2 text-theme-sm text-error-500" role="alert">
          {apiErrorMessage(q.error, 'Failed to load.')}{' '}
          <button type="button" className="underline" onClick={() => q.refetch()}>
            Retry
          </button>
        </p>
      ) : (
        <div className="mt-4 grid grid-cols-2 gap-4 xl:grid-cols-4">
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
    </section>
  );
}

function Recent() {
  const q = useSessions(5);
  const rows = q.data?.pages[0]?.data ?? [];
  return (
    <section className={cardClass} aria-label="Recent workouts">
      <div className="flex items-center justify-between gap-3">
        <h2 className={heading}>Recent workouts</h2>
        <Link
          to="/workout/history"
          className="text-theme-sm font-medium text-brand-500 hover:underline"
        >
          All history
        </Link>
      </div>
      {q.isPending ? (
        <div className="mt-4 h-24 animate-pulse rounded-xl bg-gray-100 dark:bg-white/5" />
      ) : q.error ? (
        <p className="mt-2 text-theme-sm text-error-500" role="alert">
          {apiErrorMessage(q.error, 'Failed to load.')}
        </p>
      ) : rows.length === 0 ? (
        <p className={cn(muted, 'mt-2')}>No finished workouts yet.</p>
      ) : (
        <ul className="mt-2 divide-y divide-gray-100 dark:divide-gray-800">
          {rows.map((s) => (
            <li key={s.id}>
              <Link
                to={`/workout/history/${s.id}`}
                className="flex flex-wrap items-center justify-between gap-x-4 gap-y-1 py-3 hover:bg-gray-50 dark:hover:bg-white/3"
              >
                <span className="min-w-0">
                  <span className="block text-theme-sm font-medium text-gray-800 dark:text-white/90">
                    {s.name || 'Workout'}
                  </span>
                  <span className={cn(muted, 'block')}>
                    {formatDay(s.started_at)} ·{' '}
                    {formatDuration(durationMinutes(s.started_at, s.finished_at))} · {s.set_count}{' '}
                    sets · {formatVolume(s.volume_kg)}
                  </span>
                </span>
                <HealthBadge status={s.health_status} />
              </Link>
            </li>
          ))}
        </ul>
      )}
    </section>
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
    <li key={r.id} className="flex flex-wrap items-center justify-between gap-3 py-3">
      <span className="min-w-0">
        <span className="block text-theme-sm font-medium text-gray-800 dark:text-white/90">
          {r.name}
          {r.weekday === today && (
            <span className="ms-2 rounded-full bg-brand-50 px-2 py-0.5 text-theme-xs text-brand-500 dark:bg-brand-500/12 dark:text-brand-400">
              today
            </span>
          )}
        </span>
        <span className={cn(muted, 'block')}>
          {weekdayLabel(r.weekday)} · {r.exercise_count}{' '}
          {r.exercise_count === 1 ? 'exercise' : 'exercises'}
        </span>
      </span>
      <button
        type="button"
        onClick={() => start.mutate(r.id)}
        disabled={start.isPending}
        className={r.weekday === today ? primaryButton : secondaryButton}
      >
        <Play className="size-4" aria-hidden="true" />
        Start
      </button>
    </li>
  );

  return (
    <WorkoutFrame title="Workout">
      {active.isPending || routines.isPending ? (
        <div
          className="h-40 animate-pulse rounded-2xl bg-gray-100 dark:bg-white/5"
          aria-label="Loading"
        />
      ) : active.error ? (
        <LoadError error={active.error} onRetry={() => active.refetch()} />
      ) : active.data ? (
        <section className={cardClass} aria-label="Workout in progress">
          <h2 className={heading}>{active.data.name || 'Workout'} is in progress</h2>
          <p className={cn(muted, 'mt-1')}>
            Started {formatDay(active.data.started_at)} at {formatTime(active.data.started_at)} ·{' '}
            {workSetCount(active.data)} work sets logged
          </p>
          <Link to="/workout/session" className={cn(primaryButton, 'mt-4')}>
            <Play className="size-4" aria-hidden="true" />
            Continue
          </Link>
        </section>
      ) : (
        <section className={cardClass} aria-label="Start a workout">
          <div className="flex flex-wrap items-center justify-between gap-3">
            <h2 className={heading}>Start a workout</h2>
            <button
              type="button"
              onClick={() => start.mutate(null)}
              disabled={start.isPending}
              className={secondaryButton}
            >
              Empty workout
            </button>
          </div>
          {routines.error ? (
            <p className="mt-2 text-theme-sm text-error-500" role="alert">
              {apiErrorMessage(routines.error, 'Failed to load routines.')}
            </p>
          ) : ordered.length === 0 ? (
            <p className={cn(muted, 'mt-2')}>
              No routines yet.{' '}
              <Link
                to="/workout/routines/new"
                className="font-medium text-brand-500 hover:underline"
              >
                Create one
              </Link>{' '}
              or start an empty workout and add exercises as you go.
            </p>
          ) : (
            <ul className="mt-2 divide-y divide-gray-100 dark:divide-gray-800">
              {ordered.map(routineRow)}
            </ul>
          )}
          {start.error && (
            <p role="alert" className="mt-2 text-theme-sm text-error-500">
              {start.error}
            </p>
          )}
        </section>
      )}
      <Readiness />
      <Recent />
    </WorkoutFrame>
  );
}

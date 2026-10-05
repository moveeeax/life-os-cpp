import { Link } from 'react-router';

import { useSessions } from '@/hooks/useWorkoutSession';
import { formatDay, formatDuration, formatTime, formatVolume } from '@/lib/workout/format';
import { durationMinutes } from '@/lib/workout/session';

import { HealthBadge } from './bits';
import { LoadError, WorkoutFrame } from './frame';
import { cardClass, secondaryButton } from './styles';

/** Finished workouts, newest first. */
export function WorkoutHistoryPage() {
  const list = useSessions();
  const rows = list.data?.pages.flatMap((p) => p.data) ?? [];
  const total = list.data?.pages[0]?.total ?? 0;

  return (
    <WorkoutFrame title="History">
      {list.isPending ? (
        <div
          className="h-64 animate-pulse rounded-2xl bg-gray-100 dark:bg-white/5"
          aria-label="Loading"
        />
      ) : list.error ? (
        <LoadError error={list.error} onRetry={() => list.refetch()} />
      ) : rows.length === 0 ? (
        <p className={`${cardClass} text-theme-sm text-gray-500 dark:text-gray-400`}>
          No finished workouts yet.
        </p>
      ) : (
        <>
          <p className="text-theme-xs text-gray-500 dark:text-gray-400">
            {rows.length} of {total}
          </p>
          <ul className="flex flex-col gap-3">
            {rows.map((s) => (
              <li key={s.id}>
                <Link
                  to={`/workout/history/${s.id}`}
                  className={`${cardClass} block transition hover:border-brand-300 focus-visible:ring-3 focus-visible:ring-brand-500/30 focus-visible:outline-hidden dark:hover:border-brand-800`}
                >
                  <div className="flex flex-wrap items-center justify-between gap-2">
                    <h2 className="text-base font-semibold text-gray-800 dark:text-white/90">
                      {s.name || 'Workout'}
                    </h2>
                    <HealthBadge status={s.health_status} />
                  </div>
                  <p className="mt-1 text-theme-sm text-gray-500 dark:text-gray-400">
                    {formatDay(s.started_at)}, {formatTime(s.started_at)} ·{' '}
                    {formatDuration(durationMinutes(s.started_at, s.finished_at))}
                  </p>
                  <p className="mt-1 text-theme-sm text-gray-700 tabular-nums dark:text-gray-300">
                    {s.exercise_count} {s.exercise_count === 1 ? 'exercise' : 'exercises'} ·{' '}
                    {s.set_count} work {s.set_count === 1 ? 'set' : 'sets'} ·{' '}
                    {formatVolume(s.volume_kg)}
                    {s.hr_avg != null && ` · ${s.hr_avg} bpm mean`}
                  </p>
                </Link>
              </li>
            ))}
          </ul>
          {list.hasNextPage && (
            <button
              type="button"
              onClick={() => list.fetchNextPage()}
              disabled={list.isFetchingNextPage}
              className={`${secondaryButton} self-center`}
            >
              {list.isFetchingNextPage ? 'Loading…' : 'Show more'}
            </button>
          )}
        </>
      )}
    </WorkoutFrame>
  );
}

import { Link } from 'react-router';

import { useSessions } from '@/hooks/useWorkoutSession';
import { formatDay, formatDuration, formatTime, formatVolume } from '@/lib/workout/format';
import { durationMinutes } from '@/lib/workout/session';

import { HealthBadge } from './bits';
import { LoadError, WorkoutFrame } from './frame';
import { Num, Row } from '@/shell/kit';
import { secondaryButton } from '@/shell/kit/styles';

/** Finished workouts, newest first. */
export function WorkoutHistoryPage() {
  const list = useSessions();
  const rows = list.data?.pages.flatMap((p) => p.data) ?? [];
  const total = list.data?.pages[0]?.total ?? 0;

  return (
    <WorkoutFrame title="History">
      {list.isPending ? (
        <div className="k-placeholder" aria-label="Loading" />
      ) : list.error ? (
        <LoadError error={list.error} onRetry={() => list.refetch()} />
      ) : rows.length === 0 ? (
        <p className="k-empty">No finished workouts yet.</p>
      ) : (
        <>
          <p className="k-sub" data-num-ok>
            {rows.length} of {total}
          </p>
          <div className="k-group-rows">
            {rows.map((s) => (
              <Row
                key={s.id}
                title={s.name || 'Workout'}
                sub={
                  <>
                    {formatDay(s.started_at)}, {formatTime(s.started_at)} ·{' '}
                    {formatDuration(durationMinutes(s.started_at, s.finished_at))} ·{' '}
                    <HealthBadge status={s.health_status} />
                  </>
                }
                right={<Num>{formatVolume(s.volume_kg)}</Num>}
              >
                <span>
                  <Num>{s.exercise_count}</Num> {s.exercise_count === 1 ? 'exercise' : 'exercises'}{' '}
                  · <Num>{s.set_count}</Num> work {s.set_count === 1 ? 'set' : 'sets'}
                  {s.hr_avg != null && (
                    <>
                      {' '}
                      · <Num>{s.hr_avg} bpm</Num> mean
                    </>
                  )}
                </span>
                <span className="k-actions">
                  <Link to={`/workout/history/${s.id}`} className="k-link">
                    Open
                  </Link>
                </span>
              </Row>
            ))}
          </div>
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

import { Link } from 'react-router';

import { useRoutines } from '@/hooks/useWorkout';
import { weekdayLabel } from '@/lib/workout';

import { LoadError, WorkoutFrame } from './frame';
import { Num, Row } from '@/shell/kit';
import { primaryButton } from '@/shell/kit/styles';

/** The user's routines; a card opens the editor. */
export function WorkoutRoutinesPage() {
  const routines = useRoutines();
  const rows = routines.data ?? [];

  return (
    <WorkoutFrame
      title="Routines"
      actions={
        <Link to="/workout/routines/new" className={primaryButton}>
          New routine
        </Link>
      }
    >
      {routines.isPending ? (
        <div className="k-placeholder" aria-label="Loading" />
      ) : routines.error ? (
        <LoadError error={routines.error} onRetry={() => routines.refetch()} />
      ) : rows.length === 0 ? (
        <p className="k-empty">
          No routines yet. A routine is a list of exercises with target sets, a rep range and rest;
          a workout can be started from it.
        </p>
      ) : (
        <div className="k-group-rows">
          {rows.map((r) => (
            <Row
              key={r.id}
              title={
                <Link to={`/workout/routines/${r.id}`} className="k-link-plain">
                  {r.name}
                </Link>
              }
              sub={
                <>
                  {weekdayLabel(r.weekday)} · <Num>{r.exercise_count}</Num>{' '}
                  {r.exercise_count === 1 ? 'exercise' : 'exercises'}
                  {r.note && ` · ${r.note}`}
                </>
              }
              right={
                <Link to={`/workout/routines/${r.id}`} className="k-link">
                  Edit
                </Link>
              }
            />
          ))}
        </div>
      )}
    </WorkoutFrame>
  );
}

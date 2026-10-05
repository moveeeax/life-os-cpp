import { Plus } from 'lucide-react';
import { Link } from 'react-router';

import { useRoutines } from '@/hooks/useWorkout';
import { weekdayLabel } from '@/lib/workout';

import { LoadError, WorkoutFrame } from './frame';
import { cardClass, primaryButton } from './styles';

/** The user's routines; a card opens the editor. */
export function WorkoutRoutinesPage() {
  const routines = useRoutines();
  const rows = routines.data ?? [];

  return (
    <WorkoutFrame
      title="Routines"
      actions={
        <Link to="/workout/routines/new" className={primaryButton}>
          <Plus className="size-4" aria-hidden="true" />
          New routine
        </Link>
      }
    >
      {routines.isPending ? (
        <div className="grid grid-cols-1 gap-4 sm:grid-cols-2 xl:grid-cols-3" aria-label="Loading">
          {Array.from({ length: 3 }, (_, i) => (
            <div key={i} className="h-28 animate-pulse rounded-2xl bg-gray-100 dark:bg-white/5" />
          ))}
        </div>
      ) : routines.error ? (
        <LoadError error={routines.error} onRetry={() => routines.refetch()} />
      ) : rows.length === 0 ? (
        <div className={cardClass}>
          <p className="text-theme-sm text-gray-500 dark:text-gray-400">
            No routines yet. A routine is a list of exercises with target sets, a rep range and
            rest; a workout can be started from it.
          </p>
        </div>
      ) : (
        <ul className="grid grid-cols-1 gap-4 sm:grid-cols-2 xl:grid-cols-3">
          {rows.map((r) => (
            <li key={r.id}>
              <Link
                to={`/workout/routines/${r.id}`}
                className={`${cardClass} block h-full transition hover:border-brand-300 focus-visible:ring-3 focus-visible:ring-brand-500/30 focus-visible:outline-hidden dark:hover:border-brand-800`}
              >
                <h2 className="text-lg font-semibold text-gray-800 dark:text-white/90">{r.name}</h2>
                <p className="mt-1 text-theme-sm text-gray-500 dark:text-gray-400">
                  {weekdayLabel(r.weekday)} · {r.exercise_count}{' '}
                  {r.exercise_count === 1 ? 'exercise' : 'exercises'}
                </p>
                {r.note && (
                  <p className="mt-2 line-clamp-2 text-theme-sm text-gray-700 dark:text-gray-300">
                    {r.note}
                  </p>
                )}
              </Link>
            </li>
          ))}
        </ul>
      )}
    </WorkoutFrame>
  );
}

import { X } from 'lucide-react';

import { useMe } from '@/hooks/useMe';
import { useSetTrackingMode } from '@/hooks/useWorkout';
import { userIsAdmin } from '@/lib/auth/permissions';
import { TRACKING_MODES, capitalize, trackingLabel } from '@/lib/workout';
import type { Exercise, TrackingMode } from '@/lib/workout/types';

import { ExercisePhoto } from './ExercisePhoto';
import { iconButton, modalPanel } from './styles';

function Fact({ label, value }: { label: string; value: string }) {
  return (
    <div>
      <dt className="text-theme-xs text-gray-500 dark:text-gray-400">{label}</dt>
      <dd className="text-theme-sm text-gray-800 dark:text-white/90">{value}</dd>
    </div>
  );
}

/** One exercise in full: both photos, what it trains and how to do it. */
export function ExerciseDetails({
  exercise,
  onClose,
}: {
  exercise: Exercise;
  onClose: () => void;
}) {
  const list = (items: string[]) => (items.length ? items.map(capitalize).join(', ') : '–');
  // The mode decides which fields a set has; the library's guess can be wrong.
  const user = useMe().data ?? null;
  const editable = exercise.source === 'custom' || userIsAdmin(user);
  const setMode = useSetTrackingMode(exercise.id);
  const mode = setMode.data?.tracking_mode ?? exercise.tracking_mode;
  return (
    <div className={modalPanel}>
      <div className="flex items-start justify-between gap-3">
        <h2 id="exercise-title" className="text-lg font-semibold">
          {exercise.name}
        </h2>
        <button type="button" onClick={onClose} className={iconButton} aria-label="Close">
          <X className="size-5" aria-hidden="true" />
        </button>
      </div>

      {exercise.images.length > 0 && (
        <div className="mt-4 grid grid-cols-2 gap-3">
          {exercise.images.slice(0, 2).map((path, i) => (
            <ExercisePhoto
              key={path}
              path={path}
              alt={`${exercise.name}, ${i === 0 ? 'start' : 'end'} position`}
              className="aspect-4/3 w-full rounded-xl"
            />
          ))}
        </div>
      )}

      <dl className="mt-4 grid grid-cols-2 gap-3 sm:grid-cols-3">
        <Fact label="Primary muscles" value={list(exercise.primary_muscles)} />
        <Fact label="Secondary muscles" value={list(exercise.secondary_muscles)} />
        <Fact label="Equipment" value={exercise.equipment ? capitalize(exercise.equipment) : '–'} />
        <Fact label="Category" value={capitalize(exercise.category)} />
        {editable ? (
          <div>
            <dt>
              <label
                htmlFor="exercise-mode"
                className="text-theme-xs text-gray-500 dark:text-gray-400"
              >
                A set records
              </label>
            </dt>
            <dd>
              <select
                id="exercise-mode"
                value={mode}
                disabled={setMode.isPending}
                onChange={(e) => setMode.mutate(e.target.value as TrackingMode)}
                className="mt-0.5 h-9 w-full rounded-lg border border-gray-300 bg-transparent px-2 text-theme-sm text-gray-800 dark:border-gray-700 dark:bg-gray-900 dark:text-white/90"
              >
                {TRACKING_MODES.map((m) => (
                  <option key={m.value} value={m.value}>
                    {m.label}
                  </option>
                ))}
              </select>
            </dd>
          </div>
        ) : (
          <Fact label="A set records" value={trackingLabel(mode)} />
        )}
        <Fact label="Source" value={exercise.source === 'custom' ? 'My own' : 'Library'} />
      </dl>

      {setMode.error && (
        <p role="alert" className="mt-2 text-theme-sm text-error-500">
          {setMode.error}
        </p>
      )}

      {exercise.instructions.length > 0 && (
        <ol className="mt-4 list-decimal space-y-2 ps-5 text-theme-sm text-gray-700 dark:text-gray-300">
          {exercise.instructions.map((step, i) => (
            <li key={i}>{step}</li>
          ))}
        </ol>
      )}
    </div>
  );
}

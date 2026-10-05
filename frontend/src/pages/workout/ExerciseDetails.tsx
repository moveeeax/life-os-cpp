import { X } from 'lucide-react';

import { capitalize, trackingLabel } from '@/lib/workout';
import type { Exercise } from '@/lib/workout/types';

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
        <Fact label="A set records" value={trackingLabel(exercise.tracking_mode)} />
        <Fact label="Source" value={exercise.source === 'custom' ? 'My own' : 'Library'} />
      </dl>

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

import { useState, type FormEvent } from 'react';
import { Plus } from 'lucide-react';

import { Modal } from '@/components/Modal';
import { useCreateExercise } from '@/hooks/useWorkout';
import { EQUIPMENT, MUSCLES, TRACKING_MODES, capitalize, linesOf } from '@/lib/workout';
import type { Exercise, TrackingMode } from '@/lib/workout/types';

import { ExerciseBrowser } from './ExerciseBrowser';
import { ExerciseDetails } from './ExerciseDetails';
import { WorkoutFrame } from './frame';
import {
  inputClass,
  labelClass,
  modalPanel,
  primaryButton,
  secondaryButton,
  textareaClass,
} from './styles';

function NewExerciseForm({ onClose }: { onClose: () => void }) {
  const [name, setName] = useState('');
  const [mode, setMode] = useState<TrackingMode>('weight_reps');
  const [muscle, setMuscle] = useState('');
  const [equipment, setEquipment] = useState('');
  const [instructions, setInstructions] = useState('');
  const create = useCreateExercise(onClose);

  const submit = (e: FormEvent) => {
    e.preventDefault();
    if (!name.trim()) return;
    create.mutate({
      name: name.trim(),
      tracking_mode: mode,
      equipment,
      primary_muscles: muscle ? [muscle] : [],
      instructions: linesOf(instructions),
    });
  };

  return (
    <form onSubmit={submit} className={modalPanel} aria-labelledby="new-exercise-title">
      <h2 id="new-exercise-title" className="text-lg font-semibold">
        New exercise
      </h2>
      <div className="mt-4 grid grid-cols-1 gap-4 sm:grid-cols-2">
        <div className="sm:col-span-2">
          <label htmlFor="ex-name" className={labelClass}>
            Name
          </label>
          <input
            id="ex-name"
            value={name}
            onChange={(e) => setName(e.target.value)}
            maxLength={120}
            required
            className={inputClass}
          />
        </div>
        <div className="sm:col-span-2">
          <label htmlFor="ex-mode" className={labelClass}>
            A set records
          </label>
          <select
            id="ex-mode"
            value={mode}
            onChange={(e) => setMode(e.target.value as TrackingMode)}
            className={inputClass}
          >
            {TRACKING_MODES.map((m) => (
              <option key={m.value} value={m.value}>
                {m.label}
              </option>
            ))}
          </select>
        </div>
        <div>
          <label htmlFor="ex-muscle" className={labelClass}>
            Primary muscle
          </label>
          <select
            id="ex-muscle"
            value={muscle}
            onChange={(e) => setMuscle(e.target.value)}
            className={inputClass}
          >
            <option value="">Not set</option>
            {MUSCLES.map((m) => (
              <option key={m} value={m}>
                {capitalize(m)}
              </option>
            ))}
          </select>
        </div>
        <div>
          <label htmlFor="ex-equipment" className={labelClass}>
            Equipment
          </label>
          <select
            id="ex-equipment"
            value={equipment}
            onChange={(e) => setEquipment(e.target.value)}
            className={inputClass}
          >
            <option value="">Not set</option>
            {EQUIPMENT.map((m) => (
              <option key={m} value={m}>
                {capitalize(m)}
              </option>
            ))}
          </select>
        </div>
        <div className="sm:col-span-2">
          <label htmlFor="ex-steps" className={labelClass}>
            Instructions, one step per line
          </label>
          <textarea
            id="ex-steps"
            value={instructions}
            onChange={(e) => setInstructions(e.target.value)}
            rows={4}
            className={textareaClass}
          />
        </div>
      </div>
      {create.error && (
        <p role="alert" className="mt-3 text-theme-sm text-error-500">
          {create.error}
        </p>
      )}
      <div className="mt-5 flex flex-wrap justify-end gap-3">
        <button type="button" onClick={onClose} className={secondaryButton}>
          Cancel
        </button>
        <button type="submit" disabled={create.isPending || !name.trim()} className={primaryButton}>
          {create.isPending ? 'Saving…' : 'Create'}
        </button>
      </div>
    </form>
  );
}

/** Exercise library: the seeded exercises and the user's own. */
export function WorkoutExercisesPage() {
  const [open, setOpen] = useState<Exercise | null>(null);
  const [creating, setCreating] = useState(false);

  return (
    <WorkoutFrame
      title="Exercises"
      actions={
        <button type="button" onClick={() => setCreating(true)} className={primaryButton}>
          <Plus className="size-4" aria-hidden="true" />
          New exercise
        </button>
      }
    >
      <ExerciseBrowser onSelect={setOpen} />
      {open && (
        <Modal onClose={() => setOpen(null)}>
          <ExerciseDetails exercise={open} onClose={() => setOpen(null)} />
        </Modal>
      )}
      {creating && (
        <Modal onClose={() => setCreating(false)} className="max-w-xl">
          <NewExerciseForm onClose={() => setCreating(false)} />
        </Modal>
      )}
    </WorkoutFrame>
  );
}

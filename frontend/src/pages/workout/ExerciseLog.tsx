import { useState, type FormEvent } from 'react';
import { Pencil, Trash2 } from 'lucide-react';

import { Modal } from '@/components/Modal';
import { useExercise } from '@/hooks/useWorkoutSession';
import { apiErrorMessage } from '@/lib/api/client';
import { targetSummary } from '@/lib/workout';
import {
  FIELD_LABELS,
  describeSet,
  fieldsFor,
  fieldsOf,
  mergeSets,
  parseSetFields,
  prefill,
  type DisplaySet,
  type QueuedSet,
  type SetField,
  type SetFields,
} from '@/lib/workout/session';
import { newSetBody, type SetValues } from '@/lib/workout/bodies';
import type { WorkoutSessionExercise, WorkoutSetInput } from '@/lib/workout/types';
import { cn } from '@/lib/utils';

import { ExerciseDetails } from './ExerciseDetails';
import { ExercisePhoto } from './ExercisePhoto';
import {
  cardClass,
  iconButton,
  modalPanel,
  primaryButton,
  secondaryButton,
} from '@/shell/kit/styles';

interface SetFormProps {
  exercise: WorkoutSessionExercise;
  initial: SetFields;
  initialWarmup: boolean;
  submitLabel: string;
  onSubmit: (values: SetValues, warmup: boolean) => void;
  onCancel?: () => void;
}

/** The fields of one set, sized for a thumb. */
function SetForm({
  exercise,
  initial,
  initialWarmup,
  submitLabel,
  onSubmit,
  onCancel,
}: SetFormProps) {
  const mode = exercise.tracking_mode;
  const [fields, setFields] = useState(initial);
  const [warmup, setWarmup] = useState(initialWarmup);
  const [errors, setErrors] = useState<Partial<Record<SetField, string>>>({});
  const id = (field: string) => `set-${exercise.id}-${field}`;

  const submit = (e: FormEvent) => {
    e.preventDefault();
    const parsed = parseSetFields(fields, mode);
    setErrors(parsed.errors);
    if (Object.keys(parsed.errors).length === 0) onSubmit(parsed.values, warmup);
  };

  return (
    <form onSubmit={submit} noValidate className="flex flex-col gap-3">
      <div className="grid grid-cols-2 gap-3">
        {fieldsFor(mode).map((field) => (
          <div key={field} className={fieldsFor(mode).length === 1 ? 'col-span-2' : undefined}>
            <label htmlFor={id(field)} className="mb-1 block text-theme-xs font-medium k-muted">
              {FIELD_LABELS[mode][field]}
            </label>
            <input
              id={id(field)}
              inputMode={field === 'weight' || field === 'distance' ? 'decimal' : 'numeric'}
              autoComplete="off"
              value={fields[field]}
              onChange={(e) => setFields({ ...fields, [field]: e.target.value })}
              onFocus={(e) => e.target.select()}
              aria-invalid={errors[field] ? true : undefined}
              aria-describedby={errors[field] ? `${id(field)}-error` : undefined}
              className="k-input k-big num"
            />
            {errors[field] && (
              <p id={`${id(field)}-error`} className="mt-1 text-theme-xs k-error-text">
                {errors[field]}
              </p>
            )}
          </div>
        ))}
      </div>
      <label className="flex items-center gap-2 text-theme-sm k-soft">
        <input
          type="checkbox"
          checked={warmup}
          onChange={(e) => setWarmup(e.target.checked)}
          className="size-5 rounded border-gray-300 accent-brand-500"
        />
        Warm-up set (not counted in volume)
      </label>
      <div className="flex gap-3">
        <button type="submit" className={cn(primaryButton, 'h-14 flex-1 text-base')}>
          {submitLabel}
        </button>
        {onCancel && (
          <button type="button" onClick={onCancel} className={cn(secondaryButton, 'h-14')}>
            Cancel
          </button>
        )}
      </div>
    </form>
  );
}

interface ExerciseLogProps {
  exercise: WorkoutSessionExercise;
  /** The unsent sets of the session. */
  queue: readonly QueuedSet[];
  onPut: (id: string, body: WorkoutSetInput) => void;
  onRemove: (id: string, onServer: boolean) => Promise<void>;
  /** Called after a new set was logged (not after an edit). */
  onLogged?: () => void;
}

/**
 * One exercise of a session: its targets, the sets of the previous session,
 * the logged sets and the form for the next one. Used by the active session
 * and, for corrections, by a finished one.
 */
export function ExerciseLog({ exercise, queue, onPut, onRemove, onLogged }: ExerciseLogProps) {
  const mode = exercise.tracking_mode;
  const sets = mergeSets(exercise.sets, queue, exercise.id);
  const [editing, setEditing] = useState<DisplaySet | null>(null);
  const [showDetails, setShowDetails] = useState(false);
  const [removeError, setRemoveError] = useState<string | null>(null);
  const details = useExercise(exercise.exercise_id, showDetails);

  const previous = exercise.previous_sets.filter((s) => s.kind === 'work');
  const onServer = (set: DisplaySet) => exercise.sets.some((s) => s.id === set.id);

  const log = (values: SetValues, warmup: boolean) => {
    onPut(
      crypto.randomUUID(),
      newSetBody({
        exerciseId: exercise.id,
        positions: sets.map((x) => x.position),
        values,
        warmup,
        now: new Date().toISOString(),
      }),
    );
    onLogged?.();
  };

  const saveEdit = (set: DisplaySet, values: SetValues, warmup: boolean) => {
    onPut(set.id, {
      session_exercise_id: exercise.id,
      position: set.position,
      kind: warmup ? 'warmup' : 'work',
      ...values,
      rpe: set.rpe,
      ...(set.completed_at ? { completed_at: set.completed_at } : {}),
    });
    setEditing(null);
  };

  const remove = async (set: DisplaySet) => {
    setRemoveError(null);
    if (editing?.id === set.id) setEditing(null);
    try {
      await onRemove(set.id, onServer(set));
    } catch (e) {
      setRemoveError(apiErrorMessage(e, 'Could not delete the set.'));
    }
  };

  let workNumber = 0;

  return (
    <section className={cardClass} aria-label={exercise.exercise_name}>
      <div className="flex items-center gap-3">
        <button
          type="button"
          onClick={() => setShowDetails(true)}
          className="shrink-0 rounded-lg focus-visible:ring-3 focus-visible:ring-brand-500/30 focus-visible:outline-hidden"
          aria-label={`How to do ${exercise.exercise_name}`}
        >
          <ExercisePhoto path={exercise.images[0]} alt="" className="size-16 rounded-lg" />
        </button>
        <div className="min-w-0">
          <h2 className="k-ex-title">{exercise.exercise_name}</h2>
          <p className="text-theme-sm k-muted">
            <span className="num">{targetSummary(exercise)}</span>
          </p>
        </div>
      </div>
      {exercise.note && <p className="mt-2 text-theme-sm k-soft">{exercise.note}</p>}
      {previous.length > 0 && (
        <p className="mt-2 text-theme-sm k-muted">
          Last time:{' '}
          <span className="num">{previous.map((s) => describeSet(s, mode)).join(' · ')}</span>
        </p>
      )}

      {sets.length > 0 && (
        <ol className="mt-4 k-divide">
          {sets.map((set) => {
            const tag = set.kind === 'warmup' ? 'W' : String(++workNumber);
            return (
              <li key={set.id} className="flex items-center gap-3 py-2">
                <span
                  className="k-set-tag"
                  data-num-ok
                  title={set.kind === 'warmup' ? 'Warm-up set' : `Set ${tag}`}
                >
                  {tag}
                </span>
                <span className="min-w-0 flex-1">
                  <span className="num k-set">{describeSet(set, mode)}</span>
                  {set.rejected ? (
                    <span role="alert" className="block text-theme-xs k-error-text">
                      Not saved: {set.rejected}
                    </span>
                  ) : (
                    set.unsent && (
                      <span className="block text-theme-xs k-warn-text">
                        Not saved yet, will retry
                      </span>
                    )
                  )}
                </span>
                <button
                  type="button"
                  onClick={() => setEditing(set)}
                  className={iconButton}
                  aria-label={`Edit set ${tag}`}
                >
                  <Pencil className="size-4" aria-hidden="true" />
                </button>
                <button
                  type="button"
                  onClick={() => void remove(set)}
                  className={iconButton}
                  aria-label={`Delete set ${tag}`}
                >
                  <Trash2 className="size-4" aria-hidden="true" />
                </button>
              </li>
            );
          })}
        </ol>
      )}
      {removeError && (
        <p role="alert" className="mt-2 text-theme-sm k-error-text">
          {removeError}
        </p>
      )}

      <div className="mt-4">
        {editing ? (
          <>
            <p className="mb-2 text-theme-sm font-medium k-soft">Editing a logged set</p>
            <SetForm
              key={`edit-${editing.id}`}
              exercise={exercise}
              initial={fieldsOf(editing)}
              initialWarmup={editing.kind === 'warmup'}
              submitLabel="Save set"
              onSubmit={(values, warmup) => saveEdit(editing, values, warmup)}
              onCancel={() => setEditing(null)}
            />
          </>
        ) : (
          <SetForm
            // A new form per set: the fields start from the pre-fill for that set.
            key={`new-${sets.length}`}
            exercise={exercise}
            initial={prefill({ sets, previous_sets: exercise.previous_sets })}
            initialWarmup={false}
            submitLabel="Done"
            onSubmit={log}
          />
        )}
      </div>

      {showDetails && (
        <Modal onClose={() => setShowDetails(false)}>
          {details.data ? (
            <ExerciseDetails exercise={details.data} onClose={() => setShowDetails(false)} />
          ) : (
            <div className={modalPanel}>
              <p className="text-theme-sm k-muted">
                {details.error ? apiErrorMessage(details.error, 'Failed to load.') : 'Loading…'}
              </p>
              <button
                type="button"
                onClick={() => setShowDetails(false)}
                className={cn(secondaryButton, 'mt-4')}
              >
                Close
              </button>
            </div>
          )}
        </Modal>
      )}
    </section>
  );
}

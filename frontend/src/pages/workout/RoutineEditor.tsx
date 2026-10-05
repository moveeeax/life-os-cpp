import { useMemo, useState, type FormEvent } from 'react';
import { ArrowDown, ArrowUp, Plus, Trash2 } from 'lucide-react';
import { Link, useNavigate, useParams } from 'react-router';

import { ConfirmDialog } from '@/components/ConfirmDialog';
import { Modal } from '@/components/Modal';
import { useDeleteRoutine, useRoutine, useSaveRoutine } from '@/hooks/useWorkout';
import {
  LIMITS,
  WEEKDAYS,
  draftFromRoutine,
  emptyDraft,
  itemFromExercise,
  moveItem,
  toRoutineInput,
  trackingLabel,
  usesReps,
  validateDraft,
  type DraftItem,
  type RoutineDraft,
} from '@/lib/workout';
import { cn } from '@/lib/utils';

import { ExerciseBrowser } from './ExerciseBrowser';
import { ExercisePhoto } from './ExercisePhoto';
import { FieldError, LoadError, WorkoutFrame } from './frame';
import {
  cardClass,
  dangerButton,
  iconButton,
  inputClass,
  labelClass,
  modalPanel,
  primaryButton,
  secondaryButton,
  textareaClass,
} from './styles';

const LIST = '/workout/routines';

interface NumberFieldProps {
  id: string;
  label: string;
  value: string;
  error?: string;
  onChange: (value: string) => void;
}

function NumberField({ id, label, value, error, onChange }: NumberFieldProps) {
  return (
    <div>
      <label htmlFor={id} className={labelClass}>
        {label}
      </label>
      <input
        id={id}
        inputMode="numeric"
        value={value}
        onChange={(e) => onChange(e.target.value)}
        aria-invalid={error ? true : undefined}
        aria-describedby={error ? `${id}-error` : undefined}
        className={inputClass}
      />
      <FieldError id={`${id}-error`} message={error} />
    </div>
  );
}

interface ItemCardProps {
  item: DraftItem;
  index: number;
  count: number;
  errors: Record<string, string>;
  onChange: (patch: Partial<DraftItem>) => void;
  onMove: (delta: number) => void;
  onRemove: () => void;
}

function ItemCard({ item, index, count, errors, onChange, onMove, onRemove }: ItemCardProps) {
  const id = (field: string) => `item-${item.key}-${field}`;
  const error = (field: string) => errors[`${item.key}.${field}`];
  return (
    <li className={cardClass}>
      <div className="flex items-center gap-3">
        <ExercisePhoto
          path={item.images[0]}
          alt={item.exercise_name}
          className="size-14 shrink-0 rounded-lg"
        />
        <div className="min-w-0 flex-1">
          <h3 className="truncate text-theme-sm font-semibold text-gray-800 dark:text-white/90">
            {index + 1}. {item.exercise_name}
          </h3>
          <p className="text-theme-xs text-gray-500 dark:text-gray-400">
            {trackingLabel(item.tracking_mode)}
          </p>
        </div>
        <div className="flex shrink-0 gap-1.5">
          <button
            type="button"
            onClick={() => onMove(-1)}
            disabled={index === 0}
            className={iconButton}
            aria-label={`Move ${item.exercise_name} up`}
          >
            <ArrowUp className="size-4" aria-hidden="true" />
          </button>
          <button
            type="button"
            onClick={() => onMove(1)}
            disabled={index === count - 1}
            className={iconButton}
            aria-label={`Move ${item.exercise_name} down`}
          >
            <ArrowDown className="size-4" aria-hidden="true" />
          </button>
          <button
            type="button"
            onClick={onRemove}
            className={iconButton}
            aria-label={`Remove ${item.exercise_name}`}
          >
            <Trash2 className="size-4" aria-hidden="true" />
          </button>
        </div>
      </div>

      <div className="mt-4 grid grid-cols-2 gap-3 sm:grid-cols-4">
        <NumberField
          id={id('sets')}
          label="Sets"
          value={item.sets}
          error={error('sets')}
          onChange={(sets) => onChange({ sets })}
        />
        {usesReps(item.tracking_mode) ? (
          <>
            <NumberField
              id={id('repsMin')}
              label="Reps from"
              value={item.repsMin}
              error={error('repsMin')}
              onChange={(repsMin) => onChange({ repsMin })}
            />
            <NumberField
              id={id('repsMax')}
              label="Reps to"
              value={item.repsMax}
              error={error('repsMax')}
              onChange={(repsMax) => onChange({ repsMax })}
            />
          </>
        ) : (
          <NumberField
            id={id('duration')}
            label="Duration, s"
            value={item.duration}
            error={error('duration')}
            onChange={(duration) => onChange({ duration })}
          />
        )}
        <NumberField
          id={id('rest')}
          label="Rest, s"
          value={item.rest}
          error={error('rest')}
          onChange={(rest) => onChange({ rest })}
        />
      </div>

      <div className="mt-3">
        <label htmlFor={id('note')} className={labelClass}>
          Note
        </label>
        <input
          id={id('note')}
          value={item.note}
          onChange={(e) => onChange({ note: e.target.value })}
          maxLength={LIMITS.note}
          className={inputClass}
        />
      </div>
    </li>
  );
}

interface EditorProps {
  routineId: string;
  isNew: boolean;
  initial: RoutineDraft;
}

function Editor({ routineId, isNew, initial }: EditorProps) {
  const navigate = useNavigate();
  const [draft, setDraft] = useState(initial);
  // Errors appear after the first attempt to save and then follow the draft.
  const [submitted, setSubmitted] = useState(false);
  const [picking, setPicking] = useState(false);
  const [confirmingDelete, setConfirmingDelete] = useState(false);

  const back = () => navigate(LIST);
  const save = useSaveRoutine(routineId, back);
  const remove = useDeleteRoutine(routineId, back);

  const errors = submitted ? validateDraft(draft) : {};
  const setItems = (items: DraftItem[]) => setDraft((d) => ({ ...d, items }));

  const submit = (e: FormEvent) => {
    e.preventDefault();
    setSubmitted(true);
    if (Object.keys(validateDraft(draft)).length > 0) return;
    save.mutate(toRoutineInput(draft));
  };

  return (
    <>
      <form onSubmit={submit} className="flex flex-col gap-4 md:gap-6" noValidate>
        <div className={cn(cardClass, 'grid grid-cols-1 gap-4 sm:grid-cols-2')}>
          <div>
            <label htmlFor="routine-name" className={labelClass}>
              Name
            </label>
            <input
              id="routine-name"
              value={draft.name}
              onChange={(e) => setDraft({ ...draft, name: e.target.value })}
              maxLength={LIMITS.name}
              aria-invalid={errors.name ? true : undefined}
              aria-describedby={errors.name ? 'routine-name-error' : undefined}
              className={inputClass}
            />
            <FieldError id="routine-name-error" message={errors.name} />
          </div>
          <div>
            <label htmlFor="routine-weekday" className={labelClass}>
              Day of the week
            </label>
            <select
              id="routine-weekday"
              value={draft.weekday ?? ''}
              onChange={(e) =>
                setDraft({ ...draft, weekday: e.target.value ? Number(e.target.value) : null })
              }
              className={inputClass}
            >
              <option value="">Any day</option>
              {WEEKDAYS.map((day, i) => (
                <option key={day} value={i + 1}>
                  {day}
                </option>
              ))}
            </select>
          </div>
          <div className="sm:col-span-2">
            <label htmlFor="routine-note" className={labelClass}>
              Note
            </label>
            <textarea
              id="routine-note"
              value={draft.note}
              onChange={(e) => setDraft({ ...draft, note: e.target.value })}
              rows={2}
              maxLength={LIMITS.note}
              className={textareaClass}
            />
          </div>
        </div>

        {draft.items.length === 0 ? (
          <p className={cn(cardClass, 'text-theme-sm text-gray-500 dark:text-gray-400')}>
            No exercises yet.
          </p>
        ) : (
          <ol className="flex flex-col gap-4">
            {draft.items.map((item, index) => (
              <ItemCard
                key={item.key}
                item={item}
                index={index}
                count={draft.items.length}
                errors={errors}
                onChange={(patch) =>
                  setItems(
                    draft.items.map((it) => (it.key === item.key ? { ...it, ...patch } : it)),
                  )
                }
                onMove={(delta) => setItems(moveItem(draft.items, index, delta))}
                onRemove={() => setItems(draft.items.filter((it) => it.key !== item.key))}
              />
            ))}
          </ol>
        )}
        {errors.items && (
          <p role="alert" className="text-theme-sm text-error-500">
            {errors.items}
          </p>
        )}

        <button
          type="button"
          onClick={() => setPicking(true)}
          disabled={draft.items.length >= LIMITS.exercises}
          className={cn(secondaryButton, 'self-start')}
        >
          <Plus className="size-4" aria-hidden="true" />
          Add exercise
        </button>

        {(save.error || remove.error) && (
          <p role="alert" className="text-theme-sm text-error-500">
            {save.error ?? remove.error}
          </p>
        )}
        {submitted && Object.keys(errors).length > 0 && (
          <p role="alert" className="text-theme-sm text-error-500">
            Some fields need fixing before the routine can be saved.
          </p>
        )}

        <div className="flex flex-wrap items-center gap-3">
          <button type="submit" disabled={save.isPending} className={primaryButton}>
            {save.isPending ? 'Saving…' : 'Save routine'}
          </button>
          <Link to={LIST} className={secondaryButton}>
            Cancel
          </Link>
          {!isNew && (
            <button
              type="button"
              onClick={() => setConfirmingDelete(true)}
              disabled={remove.isPending}
              className={cn(dangerButton, 'ms-auto')}
            >
              Delete
            </button>
          )}
        </div>
      </form>
      {/* Outside the form: Enter in the search field must not save the routine. */}
      {picking && (
        <Modal onClose={() => setPicking(false)} className="max-w-5xl">
          <div className={modalPanel}>
            <div className="mb-4 flex items-center justify-between gap-3">
              <h2 className="text-lg font-semibold">Add exercise</h2>
              <button type="button" onClick={() => setPicking(false)} className={secondaryButton}>
                Close
              </button>
            </div>
            <ExerciseBrowser
              onSelect={(exercise) => {
                setItems([...draft.items, itemFromExercise(exercise, crypto.randomUUID())]);
                setPicking(false);
              }}
            />
          </div>
        </Modal>
      )}
      {confirmingDelete && (
        <ConfirmDialog
          title={`Delete “${initial.name}”?`}
          description="The routine is removed. Workouts already logged from it stay."
          confirmLabel="Delete"
          destructive
          busy={remove.isPending}
          onConfirm={() => remove.mutate()}
          onClose={() => setConfirmingDelete(false)}
        />
      )}
    </>
  );
}

/**
 * Create or edit a routine. `/workout/routines/new` makes the id here: the
 * API creates the routine on the first PUT to it.
 */
export function WorkoutRoutineEditorPage() {
  const { id = 'new' } = useParams();
  const isNew = id === 'new';
  const [newId] = useState(() => crypto.randomUUID());
  const routineId = isNew ? newId : id;
  const routine = useRoutine(routineId, !isNew);

  // The form copies this once (it is keyed by the routine) and owns the draft.
  const initial = useMemo<RoutineDraft | null>(
    () => (isNew ? emptyDraft() : routine.data ? draftFromRoutine(routine.data) : null),
    [isNew, routine.data],
  );

  return (
    <WorkoutFrame title={isNew ? 'New routine' : (routine.data?.name ?? 'Routine')}>
      {initial ? (
        <Editor key={routineId} routineId={routineId} isNew={isNew} initial={initial} />
      ) : routine.error ? (
        <LoadError error={routine.error} onRetry={() => routine.refetch()} />
      ) : (
        <div
          className="h-64 animate-pulse rounded-2xl bg-gray-100 dark:bg-white/5"
          aria-label="Loading"
        />
      )}
    </WorkoutFrame>
  );
}

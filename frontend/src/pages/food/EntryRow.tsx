import { useState, type FormEvent } from 'react';

import { ConfirmDialog } from '@/components/ConfirmDialog';
import { useDeleteEntry, useUpdateEntry } from '@/hooks/useFood';
import {
  MEALS,
  formatGrams,
  formatKcal,
  parseNumber,
  toLocalDate,
  type FoodEntry,
  type FoodEntryPatch,
  type FoodMeal,
} from '@/lib/food';

import {
  dangerButton,
  inputClass,
  labelClass,
  modalPanel,
  primaryButton,
  secondaryButton,
} from '../workout/styles';

/** One line of a meal: name, grams, kcal; a tap opens the editor. */
export function EntryRow({ entry, onOpen }: { entry: FoodEntry; onOpen: () => void }) {
  return (
    <li>
      <button
        type="button"
        onClick={onOpen}
        className="flex w-full items-center gap-3 py-2 text-start hover:bg-gray-50 dark:hover:bg-white/5"
      >
        <span className="min-w-0 flex-1">
          <span className="block truncate text-theme-sm text-gray-800 dark:text-white/90">
            {entry.name}
            {entry.estimated && (
              <span className="ms-1 text-theme-xs text-gray-500" title="Estimated">
                est.
              </span>
            )}
          </span>
          <span className="block text-theme-xs text-gray-500 dark:text-gray-400">
            {entry.grams !== null ? `${formatGrams(entry.grams)} g` : 'no weight'}
            {' · '}P {formatGrams(entry.protein_g)} · F {formatGrams(entry.fat_g)} · C{' '}
            {formatGrams(entry.carbs_g)}
          </span>
        </span>
        <span className="text-theme-sm font-medium text-gray-800 tabular-nums dark:text-white/90">
          {formatKcal(entry.kcal)}
        </span>
      </button>
    </li>
  );
}

/** Edit grams, meal, date and note of an entry, or delete it. */
export function EntryEditor({ entry, onClose }: { entry: FoodEntry; onClose: () => void }) {
  const [grams, setGrams] = useState(entry.grams !== null ? String(entry.grams) : '');
  const [meal, setMeal] = useState<FoodMeal>(entry.meal);
  const [date, setDate] = useState(entry.date);
  const [note, setNote] = useState(entry.note);
  const [confirm, setConfirm] = useState(false);
  const update = useUpdateEntry(entry.id, onClose);
  const remove = useDeleteEntry(entry.id, onClose);

  const gramsValue = parseNumber(grams);
  const gramsBad = entry.grams !== null && (gramsValue === null || gramsValue <= 0);
  const dateBad = toLocalDate(date) === null;

  const submit = (e: FormEvent) => {
    e.preventDefault();
    if (gramsBad || dateBad) return;
    const patch: FoodEntryPatch = {};
    if (entry.grams !== null && gramsValue !== null && gramsValue !== entry.grams)
      patch.grams = gramsValue;
    if (meal !== entry.meal) patch.meal = meal;
    if (date !== entry.date) patch.date = date;
    if (note !== entry.note) patch.note = note;
    if (Object.keys(patch).length === 0) {
      onClose();
      return;
    }
    update.mutate(patch);
  };

  return (
    <form onSubmit={submit} className={modalPanel} aria-labelledby="entry-editor-title">
      <h2 id="entry-editor-title" className="truncate text-lg font-semibold">
        {entry.name}
      </h2>
      <p className="mt-1 text-theme-sm text-gray-500 dark:text-gray-400">
        {formatKcal(entry.kcal)} kcal · P {formatGrams(entry.protein_g)} · F{' '}
        {formatGrams(entry.fat_g)} · C {formatGrams(entry.carbs_g)}
        {entry.item_id === null && ' · the numbers of this entry are fixed'}
      </p>
      <div className="mt-4 grid grid-cols-1 gap-4 sm:grid-cols-2">
        <div>
          <label htmlFor="entry-grams" className={labelClass}>
            Grams
          </label>
          <input
            id="entry-grams"
            inputMode="decimal"
            value={grams}
            onChange={(e) => setGrams(e.target.value)}
            disabled={entry.grams === null}
            aria-invalid={gramsBad || undefined}
            className={inputClass}
          />
        </div>
        <div>
          <label htmlFor="entry-meal" className={labelClass}>
            Meal
          </label>
          <select
            id="entry-meal"
            value={meal}
            onChange={(e) => setMeal(e.target.value as FoodMeal)}
            className={inputClass}
          >
            {MEALS.map((m) => (
              <option key={m.value} value={m.value}>
                {m.label}
              </option>
            ))}
          </select>
        </div>
        <div>
          <label htmlFor="entry-date" className={labelClass}>
            Date
          </label>
          <input
            id="entry-date"
            type="date"
            value={date}
            onChange={(e) => setDate(e.target.value)}
            aria-invalid={dateBad || undefined}
            className={inputClass}
          />
        </div>
        <div>
          <label htmlFor="entry-note" className={labelClass}>
            Note
          </label>
          <input
            id="entry-note"
            value={note}
            onChange={(e) => setNote(e.target.value)}
            maxLength={500}
            className={inputClass}
          />
        </div>
      </div>
      {(update.error || remove.error) && (
        <p role="alert" className="mt-3 text-theme-sm text-error-500">
          {update.error ?? remove.error}
        </p>
      )}
      <div className="mt-5 flex flex-wrap justify-between gap-3">
        <button type="button" onClick={() => setConfirm(true)} className={dangerButton}>
          Delete
        </button>
        <div className="flex gap-3">
          <button type="button" onClick={onClose} className={secondaryButton}>
            Cancel
          </button>
          <button
            type="submit"
            disabled={update.isPending || gramsBad || dateBad}
            className={primaryButton}
          >
            {update.isPending ? 'Saving…' : 'Save'}
          </button>
        </div>
      </div>
      {confirm && (
        <ConfirmDialog
          title="Delete this entry?"
          description={`${entry.name} is removed from the diary.`}
          confirmLabel="Delete"
          destructive
          busy={remove.isPending}
          onConfirm={() => remove.mutate()}
          onClose={() => setConfirm(false)}
        />
      )}
    </form>
  );
}

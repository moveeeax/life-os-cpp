import { useEffect, useState, type FormEvent } from 'react';
import { Plus, Trash2 } from 'lucide-react';

import { ConfirmDialog } from '@/components/ConfirmDialog';
import { Modal } from '@/components/Modal';
import { useCreateItem, useDeleteItem, useFoodItems, useUpdateItem } from '@/hooks/useFood';
import {
  formatGrams,
  formatKcal,
  parseNumber,
  type FoodItem,
  type FoodItemInput,
  type FoodServing,
} from '@/lib/food';
import { cn } from '@/lib/utils';

import {
  cardClass,
  dangerButton,
  iconButton,
  inputClass,
  labelClass,
  modalPanel,
  primaryButton,
  secondaryButton,
} from '../workout/styles';
import { FoodFrame, LoadError, Placeholder } from './frame';

const SEARCH_DELAY_MS = 300;

interface Draft {
  name: string;
  brand: string;
  per: '100g' | '100ml';
  kcal: string;
  protein_g: string;
  fat_g: string;
  carbs_g: string;
  fiber_g: string;
  sugar_g: string;
  salt_g: string;
  servings: { label: string; grams: string }[];
  archived: boolean;
}

const str = (n: number | null | undefined) => (n === null || n === undefined ? '' : String(n));

const draftOf = (item: FoodItem | null): Draft =>
  item
    ? {
        name: item.name,
        brand: item.brand,
        per: item.per,
        kcal: str(item.kcal),
        protein_g: str(item.protein_g),
        fat_g: str(item.fat_g),
        carbs_g: str(item.carbs_g),
        fiber_g: str(item.fiber_g),
        sugar_g: str(item.sugar_g),
        salt_g: str(item.salt_g),
        servings: item.servings.map((s) => ({ label: s.label, grams: String(s.grams) })),
        archived: item.archived,
      }
    : {
        name: '',
        brand: '',
        per: '100g',
        kcal: '',
        protein_g: '',
        fat_g: '',
        carbs_g: '',
        fiber_g: '',
        sugar_g: '',
        salt_g: '',
        servings: [],
        archived: false,
      };

/** The draft as the API body, or null with the first problem. */
function toInput(d: Draft): { body: FoodItemInput } | { problem: string } {
  const name = d.name.trim();
  if (!name) return { problem: 'A name is needed.' };
  const req = (v: string, label: string) => {
    const n = parseNumber(v);
    return n === null || n < 0 ? { problem: `${label} must be a number of 0 or more.` } : n;
  };
  const kcal = req(d.kcal, 'kcal');
  if (typeof kcal !== 'number') return kcal;
  const protein = req(d.protein_g || '0', 'Protein');
  if (typeof protein !== 'number') return protein;
  const fat = req(d.fat_g || '0', 'Fat');
  if (typeof fat !== 'number') return fat;
  const carbs = req(d.carbs_g || '0', 'Carbs');
  if (typeof carbs !== 'number') return carbs;
  const opt = (v: string, label: string) => {
    if (v.trim() === '') return null;
    const n = parseNumber(v);
    return n === null || n < 0 ? { problem: `${label} must be a number of 0 or more.` } : n;
  };
  const fiber = opt(d.fiber_g, 'Fiber');
  if (fiber !== null && typeof fiber !== 'number') return fiber;
  const sugar = opt(d.sugar_g, 'Sugar');
  if (sugar !== null && typeof sugar !== 'number') return sugar;
  const salt = opt(d.salt_g, 'Salt');
  if (salt !== null && typeof salt !== 'number') return salt;
  const servings: FoodServing[] = [];
  for (const s of d.servings) {
    const label = s.label.trim();
    const grams = parseNumber(s.grams);
    if (!label || grams === null || grams <= 0)
      return { problem: 'Every serving needs a label and grams above 0.' };
    servings.push({ label, grams });
  }
  return {
    body: {
      name,
      brand: d.brand.trim(),
      per: d.per,
      kcal,
      protein_g: protein,
      fat_g: fat,
      carbs_g: carbs,
      fiber_g: fiber,
      sugar_g: sugar,
      salt_g: salt,
      servings,
      archived: d.archived,
    },
  };
}

function ItemForm({ item, onClose }: { item: FoodItem | null; onClose: () => void }) {
  const [d, setD] = useState<Draft>(() => draftOf(item));
  const [problem, setProblem] = useState<string | null>(null);
  const [confirm, setConfirm] = useState(false);
  const [outcome, setOutcome] = useState<string | null>(null);
  const create = useCreateItem(onClose);
  const update = useUpdateItem(item?.id ?? '', onClose);
  const remove = useDeleteItem((result) => {
    setConfirm(false);
    setOutcome(
      result === 'deleted'
        ? 'Deleted.'
        : 'Archived: the diary still refers to it, so it is hidden instead of removed.',
    );
  });
  const busy = create.isPending || update.isPending;
  const set = (patch: Partial<Draft>) => setD((x) => ({ ...x, ...patch }));
  const setServing = (i: number, patch: Partial<{ label: string; grams: string }>) =>
    set({ servings: d.servings.map((s, j) => (j === i ? { ...s, ...patch } : s)) });

  const submit = (e: FormEvent) => {
    e.preventDefault();
    const r = toInput(d);
    if ('problem' in r) {
      setProblem(r.problem);
      return;
    }
    setProblem(null);
    if (item) update.mutate(r.body);
    else create.mutate(r.body);
  };

  const num = (key: keyof Draft, label: string, required = false) => (
    <div>
      <label htmlFor={`item-${key}`} className={labelClass}>
        {label}
      </label>
      <input
        id={`item-${key}`}
        inputMode="decimal"
        value={d[key] as string}
        onChange={(e) => set({ [key]: e.target.value })}
        required={required}
        className={inputClass}
      />
    </div>
  );

  if (outcome) {
    return (
      <div className={modalPanel}>
        <p className="text-theme-sm text-gray-800 dark:text-white/90">{outcome}</p>
        <div className="mt-5 flex justify-end">
          <button type="button" onClick={onClose} className={primaryButton}>
            Close
          </button>
        </div>
      </div>
    );
  }

  return (
    <form onSubmit={submit} className={modalPanel} aria-labelledby="item-form-title">
      <h2 id="item-form-title" className="text-lg font-semibold">
        {item ? 'Edit product' : 'New product'}
      </h2>
      {item?.source === 'off' && (
        <p className="mt-1 text-theme-xs text-gray-500">
          Copied from Open Food Facts (barcode {item.off_code}); your edits stay yours.
        </p>
      )}
      <div className="mt-4 grid grid-cols-1 gap-4 sm:grid-cols-2">
        <div className="sm:col-span-2">
          <label htmlFor="item-name" className={labelClass}>
            Name
          </label>
          <input
            id="item-name"
            value={d.name}
            onChange={(e) => set({ name: e.target.value })}
            maxLength={200}
            required
            className={inputClass}
          />
        </div>
        <div>
          <label htmlFor="item-brand" className={labelClass}>
            Brand
          </label>
          <input
            id="item-brand"
            value={d.brand}
            onChange={(e) => set({ brand: e.target.value })}
            maxLength={200}
            className={inputClass}
          />
        </div>
        <div>
          <label htmlFor="item-per" className={labelClass}>
            Nutrients per
          </label>
          <select
            id="item-per"
            value={d.per}
            onChange={(e) => set({ per: e.target.value as Draft['per'] })}
            className={inputClass}
          >
            <option value="100g">100 g</option>
            <option value="100ml">100 ml</option>
          </select>
        </div>
        {num('kcal', 'kcal', true)}
        {num('protein_g', 'Protein, g')}
        {num('fat_g', 'Fat, g')}
        {num('carbs_g', 'Carbs, g')}
        {num('fiber_g', 'Fiber, g')}
        {num('sugar_g', 'Sugar, g')}
        {num('salt_g', 'Salt, g')}
      </div>
      <fieldset className="mt-4">
        <legend className={labelClass}>Servings</legend>
        <ul className="flex flex-col gap-2">
          {d.servings.map((s, i) => (
            <li key={i} className="flex items-center gap-2">
              <input
                value={s.label}
                onChange={(e) => setServing(i, { label: e.target.value })}
                placeholder="Label"
                aria-label={`Serving ${i + 1} label`}
                className={cn(inputClass, 'h-10')}
              />
              <input
                inputMode="decimal"
                value={s.grams}
                onChange={(e) => setServing(i, { grams: e.target.value })}
                placeholder="g"
                aria-label={`Serving ${i + 1} grams`}
                className={cn(inputClass, 'h-10 w-24')}
              />
              <button
                type="button"
                onClick={() => set({ servings: d.servings.filter((_, j) => j !== i) })}
                className={iconButton}
                aria-label={`Remove serving ${i + 1}`}
              >
                <Trash2 className="size-4" aria-hidden="true" />
              </button>
            </li>
          ))}
        </ul>
        <button
          type="button"
          onClick={() => set({ servings: [...d.servings, { label: '', grams: '' }] })}
          className={cn(secondaryButton, 'mt-2 px-3 py-2')}
        >
          <Plus className="size-4" aria-hidden="true" />
          Add serving
        </button>
      </fieldset>
      {item && (
        <label className="mt-4 flex items-center gap-2 text-theme-sm">
          <input
            type="checkbox"
            checked={d.archived}
            onChange={(e) => set({ archived: e.target.checked })}
          />
          Archived (hidden from search)
        </label>
      )}
      {(problem || create.error || update.error || remove.error) && (
        <p role="alert" className="mt-3 text-theme-sm text-error-500">
          {problem ?? create.error ?? update.error ?? remove.error}
        </p>
      )}
      <div className="mt-5 flex flex-wrap justify-between gap-3">
        {item ? (
          <button type="button" onClick={() => setConfirm(true)} className={dangerButton}>
            Delete
          </button>
        ) : (
          <span />
        )}
        <div className="flex gap-3">
          <button type="button" onClick={onClose} className={secondaryButton}>
            Cancel
          </button>
          <button type="submit" disabled={busy} className={primaryButton}>
            {busy ? 'Saving…' : item ? 'Save' : 'Create'}
          </button>
        </div>
      </div>
      {confirm && item && (
        <ConfirmDialog
          title="Delete this product?"
          description="A product the diary refers to is archived instead."
          confirmLabel="Delete"
          destructive
          busy={remove.isPending}
          onConfirm={() => remove.mutate(item.id)}
          onClose={() => setConfirm(false)}
        />
      )}
    </form>
  );
}

/** Own products: search, archive toggle, create and edit. */
export function FoodItemsPage() {
  const [text, setText] = useState('');
  const [q, setQ] = useState('');
  const [archived, setArchived] = useState(false);
  const [open, setOpen] = useState<FoodItem | null | 'new'>(null);

  useEffect(() => {
    const next = text.trim();
    const timer = setTimeout(() => setQ(next), SEARCH_DELAY_MS);
    return () => clearTimeout(timer);
  }, [text]);

  const items = useFoodItems(q, archived);

  return (
    <FoodFrame
      title="Products"
      actions={
        <button type="button" onClick={() => setOpen('new')} className={primaryButton}>
          <Plus className="size-4" aria-hidden="true" />
          New product
        </button>
      }
    >
      <div className="flex flex-wrap items-center gap-3">
        <input
          type="search"
          value={text}
          onChange={(e) => setText(e.target.value)}
          placeholder="Search"
          aria-label="Search products"
          className={cn(inputClass, 'max-w-sm')}
        />
        <label className="flex items-center gap-2 text-theme-sm text-gray-700 dark:text-gray-300">
          <input
            type="checkbox"
            checked={archived}
            onChange={(e) => setArchived(e.target.checked)}
          />
          Show archived
        </label>
      </div>
      {items.isPending ? (
        <Placeholder className="h-40" />
      ) : items.isError ? (
        <LoadError error={items.error} onRetry={() => void items.refetch()} />
      ) : items.data.length === 0 ? (
        <p className={cn(cardClass, 'text-center text-theme-sm text-gray-500')}>
          {q ? 'Nothing matches.' : 'No products yet. Logging from Open Food Facts adds them here.'}
        </p>
      ) : (
        <ul className={cn(cardClass, 'divide-y divide-gray-100 p-0 dark:divide-white/5')}>
          {items.data.map((it) => (
            <li key={it.id}>
              <button
                type="button"
                onClick={() => setOpen(it)}
                className="flex w-full items-center gap-3 px-5 py-3 text-start hover:bg-gray-50 dark:hover:bg-white/5"
              >
                <span className="min-w-0 flex-1">
                  <span
                    className={cn(
                      'block truncate text-theme-sm',
                      it.archived
                        ? 'text-gray-400 line-through'
                        : 'text-gray-800 dark:text-white/90',
                    )}
                  >
                    {it.name}
                  </span>
                  <span className="block truncate text-theme-xs text-gray-500">
                    {[it.brand, it.source === 'off' ? 'Open Food Facts' : null]
                      .filter(Boolean)
                      .join(' · ')}
                    {it.servings.length > 0 &&
                      ` · ${it.servings.map((s) => `${s.label} ${formatGrams(s.grams)} g`).join(', ')}`}
                  </span>
                </span>
                <span className="text-theme-xs text-gray-500 tabular-nums">
                  {formatKcal(it.kcal)} kcal / {it.per}
                </span>
              </button>
            </li>
          ))}
        </ul>
      )}
      {open && (
        <Modal onClose={() => setOpen(null)} className="max-w-xl">
          <ItemForm item={open === 'new' ? null : open} onClose={() => setOpen(null)} />
        </Modal>
      )}
    </FoodFrame>
  );
}

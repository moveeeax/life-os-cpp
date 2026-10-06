import { useEffect, useState, type FormEvent } from 'react';
import { Loader2 } from 'lucide-react';

import {
  useCreateEntries,
  useCreateEntry,
  useFoodGoals,
  useFoodItems,
  useFoodRecent,
  useItemFromOff,
  useOffSearch,
  useParseJob,
} from '@/hooks/useFood';
import {
  draftFromJob,
  formatGrams,
  formatKcal,
  linesToEntries,
  mealLabel,
  parseErrorText,
  parseNumber,
  scaled,
  type FoodItem,
  type FoodMeal,
  type OffProduct,
  type ParseDraftLine,
} from '@/lib/food';
import { cn } from '@/lib/utils';

import {
  inputClass,
  labelClass,
  modalPanel,
  primaryButton,
  secondaryButton,
  textareaClass,
} from '../workout/styles';

const SEARCH_DELAY_MS = 300;

type Tab = 'search' | 'describe' | 'quick';

interface AddEntryProps {
  date: string;
  meal: FoodMeal;
  onClose: () => void;
}

/** The add form of a meal: search, describe (with the LLM) or a quick entry. */
export function AddEntry({ date, meal, onClose }: AddEntryProps) {
  const goals = useFoodGoals();
  const llm = goals.data?.llm_available ?? false;
  const [tab, setTab] = useState<Tab>('search');
  const label = mealLabel(meal);

  return (
    <div className={modalPanel} aria-labelledby="add-entry-title">
      <h2 id="add-entry-title" className="text-lg font-semibold">
        Add to {label}
      </h2>
      <div role="tablist" aria-label="How to add" className="mt-3 flex gap-1 overflow-x-auto">
        {(
          [
            ['search', 'Search'],
            ['describe', 'Describe'],
            ['quick', 'Quick'],
          ] as [Tab, string][]
        )
          .filter(([key]) => key !== 'describe' || llm)
          .map(([key, text]) => (
            <button
              key={key}
              type="button"
              role="tab"
              aria-selected={tab === key}
              onClick={() => setTab(key)}
              className={cn(
                'rounded-lg px-3 py-2 text-theme-sm font-medium whitespace-nowrap transition',
                tab === key
                  ? 'bg-brand-50 text-brand-500 dark:bg-brand-500/12 dark:text-brand-400'
                  : 'text-gray-500 hover:bg-gray-100 dark:text-gray-400 dark:hover:bg-white/5',
              )}
            >
              {text}
            </button>
          ))}
      </div>
      <div className="mt-4">
        {tab === 'search' && <SearchTab date={date} meal={meal} onDone={onClose} />}
        {tab === 'describe' && llm && <DescribeTab date={date} meal={meal} onDone={onClose} />}
        {tab === 'quick' && <QuickTab date={date} meal={meal} onDone={onClose} />}
      </div>
    </div>
  );
}

// ── search ─────────────────────────────────────────────────────────────────

interface TabProps {
  date: string;
  meal: FoodMeal;
  onDone: () => void;
}

/** A product picked from a list, with its serving when the source had one. */
interface Pick {
  item: FoodItem | null;
  off: OffProduct | null;
  name: string;
  brand: string;
  per: '100g' | '100ml';
  kcal: number;
  protein_g: number;
  fat_g: number;
  carbs_g: number;
  servings: { label: string; grams: number }[];
}

const pickItem = (item: FoodItem): Pick => ({ ...item, item, off: null });

const pickOff = (p: OffProduct): Pick => ({
  item: null,
  off: p,
  name: p.name,
  brand: p.brand,
  per: p.per,
  kcal: p.kcal,
  protein_g: p.protein_g,
  fat_g: p.fat_g,
  carbs_g: p.carbs_g,
  servings: p.serving_grams
    ? [{ label: p.serving_label || 'serving', grams: p.serving_grams }]
    : [],
});

function ProductLine({
  name,
  brand,
  kcal,
  per,
  onPick,
}: {
  name: string;
  brand: string;
  kcal: number;
  per: string;
  onPick: () => void;
}) {
  return (
    <li>
      <button
        type="button"
        onClick={onPick}
        className="flex w-full items-center gap-3 rounded-lg px-2 py-2 text-start hover:bg-gray-50 dark:hover:bg-white/5"
      >
        <span className="min-w-0 flex-1">
          <span className="block truncate text-theme-sm text-gray-800 dark:text-white/90">
            {name}
          </span>
          {brand && <span className="block truncate text-theme-xs text-gray-500">{brand}</span>}
        </span>
        <span className="text-theme-xs text-gray-500 tabular-nums">
          {formatKcal(kcal)} kcal / {per}
        </span>
      </button>
    </li>
  );
}

function SearchTab({ date, meal, onDone }: TabProps) {
  const [text, setText] = useState('');
  const [q, setQ] = useState('');
  const [offOn, setOffOn] = useState(false);
  const [picked, setPicked] = useState<Pick | null>(null);

  // The search runs after a pause in typing, not on every key.
  useEffect(() => {
    const next = text.trim();
    const timer = setTimeout(() => setQ(next), SEARCH_DELAY_MS);
    return () => clearTimeout(timer);
  }, [text]);
  // A new query starts from own products again.
  useEffect(() => setOffOn(false), [q]);

  const recent = useFoodRecent(q === '');
  const own = useFoodItems(q);
  const off = useOffSearch(q, offOn);

  if (picked) {
    return (
      <GramsStep
        pick={picked}
        date={date}
        meal={meal}
        onBack={() => setPicked(null)}
        onDone={onDone}
      />
    );
  }

  const ownRows = own.data ?? [];
  const recentRows = q === '' ? (recent.data ?? []) : [];

  return (
    <div className="flex flex-col gap-3">
      <input
        type="search"
        value={text}
        onChange={(e) => setText(e.target.value)}
        placeholder="Search your products"
        aria-label="Search products"
        autoFocus
        className={inputClass}
      />
      {q === '' && recentRows.length > 0 && (
        <div>
          <p className="text-theme-xs font-medium text-gray-500 uppercase">Recent</p>
          <ul className="mt-1">
            {recentRows.map((it) => (
              <ProductLine key={it.id} {...it} onPick={() => setPicked(pickItem(it))} />
            ))}
          </ul>
        </div>
      )}
      {q !== '' && (
        <div>
          <p className="text-theme-xs font-medium text-gray-500 uppercase">Your products</p>
          {own.isPending ? (
            <p className="mt-1 text-theme-sm text-gray-500">Searching…</p>
          ) : ownRows.length === 0 ? (
            <p className="mt-1 text-theme-sm text-gray-500">Nothing of yours matches.</p>
          ) : (
            <ul className="mt-1">
              {ownRows.map((it) => (
                <ProductLine key={it.id} {...it} onPick={() => setPicked(pickItem(it))} />
              ))}
            </ul>
          )}
        </div>
      )}
      {q.length >= 2 && !offOn && (
        <button type="button" onClick={() => setOffOn(true)} className={secondaryButton}>
          Search Open Food Facts
        </button>
      )}
      {offOn && (
        <div>
          <p className="text-theme-xs font-medium text-gray-500 uppercase">Open Food Facts</p>
          {off.isPending ? (
            <p className="mt-1 flex items-center gap-2 text-theme-sm text-gray-500">
              <Loader2 className="size-4 animate-spin" aria-hidden="true" /> Searching…
            </p>
          ) : off.isError ? (
            <p role="alert" className="mt-1 text-theme-sm text-error-500">
              Open Food Facts did not answer. Try again later.
            </p>
          ) : off.data.length === 0 ? (
            <p className="mt-1 text-theme-sm text-gray-500">No products found.</p>
          ) : (
            <ul className="mt-1">
              {off.data.map((p) => (
                <ProductLine key={p.code} {...p} onPick={() => setPicked(pickOff(p))} />
              ))}
            </ul>
          )}
          <p className="mt-2 text-theme-xs text-gray-500">
            Data from{' '}
            <a
              href="https://world.openfoodfacts.org"
              target="_blank"
              rel="noreferrer"
              className="underline"
            >
              Open Food Facts
            </a>
            , Open Database License.
          </p>
        </div>
      )}
    </div>
  );
}

function GramsStep({
  pick,
  date,
  meal,
  onBack,
  onDone,
}: {
  pick: Pick;
  date: string;
  meal: FoodMeal;
  onBack: () => void;
  onDone: () => void;
}) {
  const [grams, setGrams] = useState(pick.servings[0] ? String(pick.servings[0].grams) : '100');
  const g = parseNumber(grams) ?? 0;
  const create = useCreateEntry(onDone);
  // An Open Food Facts product is copied into own products first, then logged.
  const copy = useItemFromOff((item) => create.mutate({ date, meal, item_id: item.id, grams: g }));
  const preview = scaled(pick, g);
  const busy = create.isPending || copy.isPending;

  const submit = (e: FormEvent) => {
    e.preventDefault();
    if (g <= 0) return;
    if (pick.item) create.mutate({ date, meal, item_id: pick.item.id, grams: g });
    else if (pick.off) copy.mutate(pick.off.code);
  };

  return (
    <form onSubmit={submit} className="flex flex-col gap-4">
      <div>
        <p className="text-theme-sm font-medium text-gray-800 dark:text-white/90">{pick.name}</p>
        <p className="text-theme-xs text-gray-500">
          {pick.brand && `${pick.brand} · `}
          {formatKcal(pick.kcal)} kcal / {pick.per}
        </p>
      </div>
      {pick.servings.length > 0 && (
        <div className="flex flex-wrap gap-2">
          {pick.servings.map((s) => (
            <button
              key={s.label}
              type="button"
              onClick={() => setGrams(String(s.grams))}
              className={cn(
                secondaryButton,
                'px-3 py-2',
                g === s.grams && 'border-brand-500 text-brand-500',
              )}
            >
              {s.label} · {formatGrams(s.grams)} g
            </button>
          ))}
        </div>
      )}
      <div>
        <label htmlFor="pick-grams" className={labelClass}>
          {pick.per === '100ml' ? 'Millilitres' : 'Grams'}
        </label>
        <input
          id="pick-grams"
          inputMode="decimal"
          value={grams}
          onChange={(e) => setGrams(e.target.value)}
          aria-invalid={g <= 0 || undefined}
          autoFocus
          className={inputClass}
        />
      </div>
      <p className="text-theme-sm text-gray-800 tabular-nums dark:text-white/90">
        {formatKcal(preview.kcal)} kcal · P {formatGrams(preview.protein_g)} · F{' '}
        {formatGrams(preview.fat_g)} · C {formatGrams(preview.carbs_g)}
      </p>
      {(create.error || copy.error) && (
        <p role="alert" className="text-theme-sm text-error-500">
          {create.error ?? copy.error}
        </p>
      )}
      <div className="flex flex-wrap justify-end gap-3">
        <button type="button" onClick={onBack} className={secondaryButton}>
          Back
        </button>
        <button type="submit" disabled={busy || g <= 0} className={primaryButton}>
          {busy ? 'Adding…' : `Add to ${mealLabel(meal)}`}
        </button>
      </div>
    </form>
  );
}

// ── describe ───────────────────────────────────────────────────────────────

function DescribeTab({ date, meal, onDone }: TabProps) {
  const [text, setText] = useState('');
  const parse = useParseJob();
  const [lines, setLines] = useState<ParseDraftLine[]>([]);
  const add = useCreateEntries(onDone);

  // A finished job becomes the editable draft once.
  const jobId = parse.state === 'done' ? parse.job?.id : undefined;
  useEffect(() => {
    if (jobId && parse.job) setLines(draftFromJob(parse.job));
  }, [jobId, parse.job]);

  const edit = (key: string, patch: Partial<ParseDraftLine>) =>
    setLines((ls) => ls.map((l) => (l.key === key ? { ...l, ...patch } : l)));

  const field = (
    key: string,
    name: 'grams' | 'kcal' | 'protein_g' | 'fat_g' | 'carbs_g',
    value: string,
  ) => {
    const n = parseNumber(value);
    if (n !== null && n >= 0) edit(key, { [name]: n });
  };

  const selected = lines.filter((l) => l.selected);

  if (parse.state === 'starting' || parse.state === 'running') {
    return (
      <div className="flex flex-col items-center gap-3 py-6 text-center">
        <Loader2 className="size-6 animate-spin text-brand-500" aria-hidden="true" />
        <p className="text-theme-sm text-gray-500">
          {parse.state === 'starting' ? 'Sending…' : 'The model is reading your text…'}
        </p>
        <button type="button" onClick={parse.cancel} className={secondaryButton}>
          Cancel
        </button>
      </div>
    );
  }

  if (parse.state === 'done') {
    return (
      <div className="flex flex-col gap-3">
        <p className="text-theme-sm text-gray-500">
          Check the lines, fix the numbers, untick what you did not eat.
        </p>
        <ul className="flex flex-col gap-3">
          {lines.map((l) => (
            <li key={l.key} className="rounded-lg border border-gray-200 p-3 dark:border-gray-700">
              <div className="flex items-start gap-2">
                <input
                  type="checkbox"
                  checked={l.selected}
                  onChange={(e) => edit(l.key, { selected: e.target.checked })}
                  aria-label={`Include ${l.name}`}
                  className="mt-1"
                />
                <div className="flex-1">
                  <input
                    value={l.name}
                    onChange={(e) => edit(l.key, { name: e.target.value })}
                    maxLength={120}
                    aria-label="Name"
                    className={cn(inputClass, 'h-9')}
                  />
                  {l.note && <p className="mt-1 text-theme-xs text-gray-500">{l.note}</p>}
                  {l.item_id && (
                    <p className="mt-1 text-theme-xs text-gray-500">One of your products</p>
                  )}
                </div>
              </div>
              <div className="mt-2 grid grid-cols-5 gap-2">
                {(
                  [
                    ['grams', 'g'],
                    ['kcal', 'kcal'],
                    ['protein_g', 'P'],
                    ['fat_g', 'F'],
                    ['carbs_g', 'C'],
                  ] as const
                ).map(([name, short]) => (
                  <label key={name} className="text-theme-xs text-gray-500">
                    {short}
                    <input
                      inputMode="decimal"
                      value={String(l[name])}
                      onChange={(e) => field(l.key, name, e.target.value)}
                      disabled={l.item_id !== null && name !== 'grams'}
                      aria-label={`${l.name} ${short}`}
                      className={cn(inputClass, 'mt-0.5 h-9 px-2')}
                    />
                  </label>
                ))}
              </div>
            </li>
          ))}
        </ul>
        {add.error && (
          <p role="alert" className="text-theme-sm text-error-500">
            {add.error}
          </p>
        )}
        <div className="flex flex-wrap justify-end gap-3">
          <button type="button" onClick={parse.cancel} className={secondaryButton}>
            Start over
          </button>
          <button
            type="button"
            disabled={
              add.isPending || selected.length === 0 || selected.some((l) => !l.name.trim())
            }
            onClick={() => add.mutate(linesToEntries(lines, date, meal))}
            className={primaryButton}
          >
            {add.isPending ? 'Adding…' : `Add ${selected.length} selected`}
          </button>
        </div>
      </div>
    );
  }

  return (
    <form
      onSubmit={(e) => {
        e.preventDefault();
        if (text.trim()) void parse.start({ text: text.trim(), meal, date });
      }}
      className="flex flex-col gap-3"
    >
      <label htmlFor="describe-text" className={labelClass}>
        What did you eat?
      </label>
      <textarea
        id="describe-text"
        value={text}
        onChange={(e) => setText(e.target.value)}
        rows={4}
        maxLength={2000}
        placeholder="Two eggs, a slice of rye bread with butter, black coffee"
        autoFocus
        className={textareaClass}
      />
      {parse.state === 'failed' && (
        <p role="alert" className="text-theme-sm text-error-500">
          {parseErrorText(parse.error)}
        </p>
      )}
      <div className="flex justify-end">
        <button type="submit" disabled={!text.trim()} className={primaryButton}>
          {parse.state === 'failed' ? 'Try again' : 'Parse'}
        </button>
      </div>
    </form>
  );
}

// ── quick ──────────────────────────────────────────────────────────────────

function QuickTab({ date, meal, onDone }: TabProps) {
  const [name, setName] = useState('');
  const [kcal, setKcal] = useState('');
  const [protein, setProtein] = useState('');
  const [fat, setFat] = useState('');
  const [carbs, setCarbs] = useState('');
  const create = useCreateEntry(onDone);

  const k = parseNumber(kcal);
  const ok = name.trim() !== '' && k !== null && k >= 0;

  const submit = (e: FormEvent) => {
    e.preventDefault();
    if (!ok) return;
    create.mutate({
      date,
      meal,
      name: name.trim(),
      kcal: k,
      protein_g: parseNumber(protein) ?? 0,
      fat_g: parseNumber(fat) ?? 0,
      carbs_g: parseNumber(carbs) ?? 0,
    });
  };

  const num = (id: string, label: string, value: string, set: (v: string) => void) => (
    <div>
      <label htmlFor={id} className={labelClass}>
        {label}
      </label>
      <input
        id={id}
        inputMode="decimal"
        value={value}
        onChange={(e) => set(e.target.value)}
        className={inputClass}
      />
    </div>
  );

  return (
    <form onSubmit={submit} className="flex flex-col gap-4">
      <div>
        <label htmlFor="quick-name" className={labelClass}>
          Name
        </label>
        <input
          id="quick-name"
          value={name}
          onChange={(e) => setName(e.target.value)}
          maxLength={120}
          autoFocus
          className={inputClass}
        />
      </div>
      <div className="grid grid-cols-2 gap-3 sm:grid-cols-4">
        {num('quick-kcal', 'kcal', kcal, setKcal)}
        {num('quick-protein', 'Protein, g', protein, setProtein)}
        {num('quick-fat', 'Fat, g', fat, setFat)}
        {num('quick-carbs', 'Carbs, g', carbs, setCarbs)}
      </div>
      {create.error && (
        <p role="alert" className="text-theme-sm text-error-500">
          {create.error}
        </p>
      )}
      <div className="flex justify-end">
        <button type="submit" disabled={create.isPending || !ok} className={primaryButton}>
          {create.isPending ? 'Adding…' : 'Add'}
        </button>
      </div>
    </form>
  );
}

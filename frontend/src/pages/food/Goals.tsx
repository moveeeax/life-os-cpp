import { useEffect, useState, type FormEvent } from 'react';

import { useFoodGoals, useSaveGoals } from '@/hooks/useFood';
import {
  formatGrams,
  formatKcal,
  parseNumber,
  type FoodGoals,
  type FoodGoalsProfile,
} from '@/lib/food';
import { cn } from '@/lib/utils';

import { cardClass, inputClass, labelClass, primaryButton, textareaClass } from '../workout/styles';
import { FoodFrame, LoadError, Placeholder } from './frame';

const ACTIVITY: { value: FoodGoalsProfile['activity']; label: string }[] = [
  { value: 'sedentary', label: 'Sedentary (desk, little exercise) ×1.2' },
  { value: 'light', label: 'Light (1–3 workouts a week) ×1.375' },
  { value: 'moderate', label: 'Moderate (3–5 workouts a week) ×1.55' },
  { value: 'active', label: 'Active (6–7 workouts a week) ×1.725' },
  { value: 'very_active', label: 'Very active (physical job or twice a day) ×1.9' },
];

interface Draft {
  height_cm: string;
  birth_date: string;
  sex: '' | 'male' | 'female';
  activity: FoodGoalsProfile['activity'];
  target_weight_kg: string;
  pace_kg_per_week: string;
  manual_weight_kg: string;
  profile_note: string;
  kcal_override: string;
  protein_override_g: string;
  fat_override_g: string;
  carbs_override_g: string;
}

const str = (n: number | null | undefined) => (n === null || n === undefined ? '' : String(n));

const draftOf = (p: FoodGoalsProfile): Draft => ({
  height_cm: str(p.height_cm),
  birth_date: p.birth_date ?? '',
  sex: p.sex ?? '',
  activity: p.activity,
  target_weight_kg: str(p.target_weight_kg),
  pace_kg_per_week: str(p.pace_kg_per_week ?? 0.5),
  manual_weight_kg: str(p.manual_weight_kg),
  profile_note: p.profile_note ?? '',
  kcal_override: str(p.kcal_override),
  protein_override_g: str(p.protein_override_g),
  fat_override_g: str(p.fat_override_g),
  carbs_override_g: str(p.carbs_override_g),
});

const optional = (v: string): number | null => (v.trim() === '' ? null : parseNumber(v));

/** The draft as the API body, or the first problem. */
function toBody(d: Draft): { body: FoodGoalsProfile } | { problem: string } {
  const fields: [keyof Draft, string][] = [
    ['height_cm', 'Height'],
    ['target_weight_kg', 'Target weight'],
    ['manual_weight_kg', 'Weight'],
    ['kcal_override', 'kcal override'],
    ['protein_override_g', 'Protein override'],
    ['fat_override_g', 'Fat override'],
    ['carbs_override_g', 'Carbs override'],
  ];
  for (const [key, label] of fields) {
    const v = d[key] as string;
    if (v.trim() !== '' && (parseNumber(v) === null || (parseNumber(v) as number) <= 0))
      return { problem: `${label} must be a number above 0.` };
  }
  const pace = parseNumber(d.pace_kg_per_week);
  if (pace === null || pace < 0 || pace > 1.5)
    return { problem: 'Pace must be between 0 and 1.5 kg a week.' };
  return {
    body: {
      height_cm: optional(d.height_cm),
      birth_date: d.birth_date || null,
      sex: d.sex || null,
      activity: d.activity,
      target_weight_kg: optional(d.target_weight_kg),
      pace_kg_per_week: pace,
      manual_weight_kg: optional(d.manual_weight_kg),
      profile_note: d.profile_note,
      kcal_override: optional(d.kcal_override),
      protein_override_g: optional(d.protein_override_g),
      fat_override_g: optional(d.fat_override_g),
      carbs_override_g: optional(d.carbs_override_g),
    },
  };
}

const MISSING: Record<string, string> = {
  height_cm: 'height',
  birth_date: 'birth date',
  sex: 'sex',
  weight: 'weight (link a scale on the Health page or type it below)',
};

function Computed({ goals }: { goals: FoodGoals }) {
  const c = goals.computed;
  const w = goals.weight;
  if (!c) {
    return (
      <p className="text-theme-sm text-gray-500 dark:text-gray-400">
        The targets need {goals.missing.map((m) => MISSING[m] ?? m).join(', ')}.
      </p>
    );
  }
  const row = (label: string, value: string) => (
    <div className="flex justify-between gap-3 text-theme-sm">
      <span className="text-gray-500 dark:text-gray-400">{label}</span>
      <span className="text-gray-800 tabular-nums dark:text-white/90">{value}</span>
    </div>
  );
  return (
    <div className="flex flex-col gap-1.5">
      {row(
        'Weight',
        w.kg === null
          ? '–'
          : `${formatGrams(w.kg)} kg (${w.source === 'scale' ? 'from the scale' : 'typed'})`,
      )}
      {c.age_years !== undefined && row('Age', `${c.age_years}`)}
      {c.bmr !== undefined && row('BMR (Mifflin-St Jeor)', `${formatKcal(c.bmr)} kcal`)}
      {c.maintenance !== undefined && row('Maintenance', `${formatKcal(c.maintenance)} kcal`)}
      {c.deficit !== undefined &&
        row('Daily deficit for the pace', `${formatKcal(c.deficit)} kcal`)}
      {c.kcal !== undefined && row('Computed goal', `${formatKcal(c.kcal)} kcal`)}
      {c.floored && (
        <p className="text-theme-xs text-warning-600 dark:text-orange-400">
          The pace asks for less than 1,200 kcal a day; the goal is held at 1,200.
        </p>
      )}
      {c.below_bmr && (
        <p className="text-theme-xs text-warning-600 dark:text-orange-400">
          The goal is below your BMR. A slower pace is easier to keep.
        </p>
      )}
    </div>
  );
}

function GoalsForm({ goals }: { goals: FoodGoals }) {
  const [d, setD] = useState<Draft>(() => draftOf(goals.profile));
  const [problem, setProblem] = useState<string | null>(null);
  const [saved, setSaved] = useState(false);
  const save = useSaveGoals(() => setSaved(true));
  // A save answers with the new profile; the form follows it.
  useEffect(() => setD(draftOf(goals.profile)), [goals.profile]);

  const set = (patch: Partial<Draft>) => {
    setSaved(false);
    setD((x) => ({ ...x, ...patch }));
  };

  const submit = (e: FormEvent) => {
    e.preventDefault();
    const r = toBody(d);
    if ('problem' in r) {
      setProblem(r.problem);
      return;
    }
    setProblem(null);
    save.mutate(r.body);
  };

  const field = (key: keyof Draft, label: string, extra: Record<string, unknown> = {}) => (
    <div>
      <label htmlFor={`goals-${key}`} className={labelClass}>
        {label}
      </label>
      <input
        id={`goals-${key}`}
        inputMode="decimal"
        value={d[key] as string}
        onChange={(e) => set({ [key]: e.target.value })}
        className={inputClass}
        {...extra}
      />
    </div>
  );

  const c = goals.computed;
  const target = (key: keyof Draft, label: string, computed: number | undefined, unit: string) => (
    <div>
      <label htmlFor={`goals-${key}`} className={labelClass}>
        {label}
        {computed !== undefined && (
          <span className="ms-1 font-normal text-gray-500">
            computed {unit === 'kcal' ? formatKcal(computed) : formatGrams(computed)}
          </span>
        )}
      </label>
      <input
        id={`goals-${key}`}
        inputMode="decimal"
        value={d[key] as string}
        onChange={(e) => set({ [key]: e.target.value })}
        placeholder={computed !== undefined ? 'override' : ''}
        className={inputClass}
      />
    </div>
  );

  return (
    <form onSubmit={submit} className="grid gap-4 md:grid-cols-2 md:gap-6">
      <section className={cardClass} aria-labelledby="goals-profile">
        <h2 id="goals-profile" className="text-base font-semibold text-gray-800 dark:text-white/90">
          Profile
        </h2>
        <div className="mt-4 grid grid-cols-1 gap-4 sm:grid-cols-2">
          {field('height_cm', 'Height, cm')}
          <div>
            <label htmlFor="goals-birth_date" className={labelClass}>
              Birth date
            </label>
            <input
              id="goals-birth_date"
              type="date"
              value={d.birth_date}
              onChange={(e) => set({ birth_date: e.target.value })}
              className={inputClass}
            />
          </div>
          <div>
            <label htmlFor="goals-sex" className={labelClass}>
              Sex
            </label>
            <select
              id="goals-sex"
              value={d.sex}
              onChange={(e) => set({ sex: e.target.value as Draft['sex'] })}
              className={inputClass}
            >
              <option value="">Not set</option>
              <option value="male">Male</option>
              <option value="female">Female</option>
            </select>
          </div>
          <div>
            <label htmlFor="goals-activity" className={labelClass}>
              Activity
            </label>
            <select
              id="goals-activity"
              value={d.activity}
              onChange={(e) => set({ activity: e.target.value as Draft['activity'] })}
              className={inputClass}
            >
              {ACTIVITY.map((a) => (
                <option key={a.value} value={a.value}>
                  {a.label}
                </option>
              ))}
            </select>
          </div>
          {field('target_weight_kg', 'Target weight, kg')}
          {field('pace_kg_per_week', 'Pace, kg a week')}
          {goals.weight.source !== 'scale' && field('manual_weight_kg', 'Current weight, kg')}
          <div className="sm:col-span-2">
            <label htmlFor="goals-profile_note" className={labelClass}>
              Note for the text parser (tastes, usual portions, what to avoid)
            </label>
            <textarea
              id="goals-profile_note"
              value={d.profile_note}
              onChange={(e) => set({ profile_note: e.target.value })}
              rows={3}
              maxLength={1000}
              className={textareaClass}
            />
          </div>
        </div>
      </section>
      <div className="flex flex-col gap-4 md:gap-6">
        <section className={cardClass} aria-labelledby="goals-computed">
          <h2
            id="goals-computed"
            className="text-base font-semibold text-gray-800 dark:text-white/90"
          >
            Computed
          </h2>
          <div className="mt-3">
            <Computed goals={goals} />
          </div>
        </section>
        <section className={cardClass} aria-labelledby="goals-targets">
          <h2
            id="goals-targets"
            className="text-base font-semibold text-gray-800 dark:text-white/90"
          >
            Daily targets
          </h2>
          <p className="mt-1 text-theme-xs text-gray-500">
            Empty fields use the computed numbers; a value here overrides one.
          </p>
          <div className="mt-3 grid grid-cols-2 gap-3">
            {target('kcal_override', 'kcal', c?.kcal, 'kcal')}
            {target('protein_override_g', 'Protein, g', c?.protein_g, 'g')}
            {target('fat_override_g', 'Fat, g', c?.fat_g, 'g')}
            {target('carbs_override_g', 'Carbs, g', c?.carbs_g, 'g')}
          </div>
          {goals.targets && (
            <p className="mt-3 text-theme-sm text-gray-800 tabular-nums dark:text-white/90">
              In force: {formatKcal(goals.targets.kcal)} kcal · P{' '}
              {formatGrams(goals.targets.protein_g)} · F {formatGrams(goals.targets.fat_g)} · C{' '}
              {formatGrams(goals.targets.carbs_g)}
            </p>
          )}
        </section>
        {(problem || save.error) && (
          <p role="alert" className="text-theme-sm text-error-500">
            {problem ?? save.error}
          </p>
        )}
        <div className="flex items-center justify-end gap-3">
          {saved && <span className="text-theme-sm text-success-600">Saved.</span>}
          <button type="submit" disabled={save.isPending} className={cn(primaryButton)}>
            {save.isPending ? 'Saving…' : 'Save'}
          </button>
        </div>
      </div>
    </form>
  );
}

/** The goals profile and the numbers computed from it. */
export function FoodGoalsPage() {
  const goals = useFoodGoals();
  return (
    <FoodFrame title="Goals">
      {goals.isPending ? (
        <Placeholder className="h-72" />
      ) : goals.isError ? (
        <LoadError error={goals.error} onRetry={() => void goals.refetch()} />
      ) : (
        <GoalsForm goals={goals.data} />
      )}
    </FoodFrame>
  );
}

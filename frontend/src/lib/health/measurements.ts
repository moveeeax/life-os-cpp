// Girths measured by hand (/api/v1/fitness/measurements): the labels, the
// line of a row, and the body of the form.
import type { components } from '@/lib/api/schema.gen';

type S = components['schemas'];
export type TapeMeasurement = S['TapeMeasurement'];
export type TapeMeasurementInput = S['TapeMeasurementInput'];

export type Girth = 'waist_cm' | 'chest_cm' | 'hips_cm' | 'neck_cm' | 'arm_cm' | 'thigh_cm';

/** The girths in the order of the form and the API. */
export const GIRTHS: { key: Girth; label: string }[] = [
  { key: 'waist_cm', label: 'Waist' },
  { key: 'chest_cm', label: 'Chest' },
  { key: 'hips_cm', label: 'Hips' },
  { key: 'neck_cm', label: 'Neck' },
  { key: 'arm_cm', label: 'Arm' },
  { key: 'thigh_cm', label: 'Thigh' },
];

const cm = new Intl.NumberFormat('en-US', { maximumFractionDigits: 1 });

/** "82.5 cm". */
export const formatCm = (v: number): string => `${cm.format(v)} cm`;

/** The girths a row has, in order: [{label, value}]. */
export function girthsOf(m: TapeMeasurement): { label: string; value: number }[] {
  return GIRTHS.flatMap(({ key, label }) => {
    const v = m[key];
    return v == null ? [] : [{ label, value: v }];
  });
}

/** The change of each girth from the measurement before, by key; absent when either is missing. */
export function changeFrom(
  m: TapeMeasurement,
  before: TapeMeasurement | undefined,
): Partial<Record<Girth, number>> {
  const out: Partial<Record<Girth, number>> = {};
  if (!before) return out;
  for (const { key } of GIRTHS) {
    const a = m[key];
    const b = before[key];
    if (a != null && b != null) out[key] = Math.round((a - b) * 10) / 10;
  }
  return out;
}

export type MeasurementForm = Record<Girth, string> & { date: string; note: string };

export function measurementForm(today: string, m?: TapeMeasurement): MeasurementForm {
  const f = { date: m?.date ?? today, note: m?.note ?? '' } as MeasurementForm;
  for (const { key } of GIRTHS) f[key] = m?.[key] != null ? String(m[key]) : '';
  return f;
}

const num = (text: string): number => Number(text.trim().replace(',', '.'));

/** What is wrong with the form before it is sent, or null. */
export function measurementProblem(f: MeasurementForm): string | null {
  if (!/^\d{4}-\d{2}-\d{2}$/.test(f.date)) return 'Pick the day of the measurement.';
  const filled = GIRTHS.filter(({ key }) => f[key].trim() !== '');
  if (filled.length === 0) return 'Give at least one girth.';
  for (const { key, label } of filled) {
    const v = num(f[key]);
    if (!Number.isFinite(v) || v <= 0 || v >= 400) return `${label}: a number of cm, above 0.`;
  }
  return null;
}

/** The body of the form: an empty girth is sent as null, so an edit clears it. */
export function measurementBody(f: MeasurementForm): TapeMeasurementInput {
  const body: TapeMeasurementInput = { date: f.date, note: f.note.trim() };
  for (const { key } of GIRTHS) body[key] = f[key].trim() === '' ? null : num(f[key]);
  return body;
}

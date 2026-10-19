import { describe, expect, it } from 'vitest';

import {
  changeFrom,
  formatCm,
  girthsOf,
  measurementBody,
  measurementForm,
  measurementProblem,
  type TapeMeasurement,
} from './measurements';

const row = (over: Partial<TapeMeasurement> = {}): TapeMeasurement => ({
  id: 'm1',
  date: '2026-10-12',
  waist_cm: 82.5,
  chest_cm: null,
  hips_cm: 97,
  neck_cm: null,
  arm_cm: null,
  thigh_cm: null,
  note: '',
  created_at: '2026-10-12T06:00:00+00:00',
  updated_at: '2026-10-12T06:00:00+00:00',
  ...over,
});

describe('measurements by hand', () => {
  it('lists the girths a row has and the change from the one before', () => {
    expect(girthsOf(row())).toEqual([
      { label: 'Waist', value: 82.5 },
      { label: 'Hips', value: 97 },
    ]);
    expect(changeFrom(row(), row({ waist_cm: 83.4, hips_cm: null }))).toEqual({ waist_cm: -0.9 });
    expect(changeFrom(row(), undefined)).toEqual({});
    expect(formatCm(82.25)).toBe('82.3 cm');
  });

  it('sends an empty girth as null and reads a comma as a point', () => {
    const f = { ...measurementForm('2026-10-19'), waist_cm: '81,5', thigh_cm: '55' };
    expect(measurementProblem(f)).toBeNull();
    expect(measurementBody({ ...f, note: ' after run ' })).toEqual({
      date: '2026-10-19',
      note: 'after run',
      waist_cm: 81.5,
      chest_cm: null,
      hips_cm: null,
      neck_cm: null,
      arm_cm: null,
      thigh_cm: 55,
    });
    expect(measurementForm('2026-10-19', row()).hips_cm).toBe('97');
  });

  it('asks for a day and at least one girth in range', () => {
    const f = measurementForm('2026-10-19');
    expect(measurementProblem(f)).toBe('Give at least one girth.');
    expect(measurementProblem({ ...f, date: '', waist_cm: '80' })).toBe(
      'Pick the day of the measurement.',
    );
    expect(measurementProblem({ ...f, neck_cm: '400' })).toBe('Neck: a number of cm, above 0.');
    expect(measurementProblem({ ...f, arm_cm: 'x' })).toBe('Arm: a number of cm, above 0.');
  });
});

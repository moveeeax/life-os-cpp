// @vitest-environment jsdom
import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

import type { Habit } from '@/lib/habits';
import type { MetricEntry } from '@/lib/metrics';

/**
 * The habit form on a catalog source: the body carries the canonical id and the
 * direction, a presence source left without a goal sends none, and the habit
 * being edited is not offered as its own source.
 */

const { mutate } = vi.hoisted(() => ({ mutate: vi.fn() }));

const entry = (over: Partial<MetricEntry>): MetricEntry => ({
  id: 'food:kcal',
  module: 'food',
  label: 'Calories eaten',
  unit: 'kcal',
  reduce: 'sum',
  presence: false,
  available: true,
  why: null,
  ...over,
});
const catalog = [
  entry({}),
  entry({ id: 'fitness:weight', module: 'fitness', label: 'Weight', unit: 'kg', presence: true }),
  entry({ id: 'habits:h1', module: 'habits', label: 'Water', unit: 'ml', presence: true }),
  entry({ id: 'habits:h2', module: 'habits', label: 'Reading', unit: 'min', presence: true }),
];

vi.mock('@/hooks/useMetrics', () => ({
  useMetrics: () => ({ data: catalog, isPending: false }),
}));
vi.mock('@/hooks/useHabits', () => ({
  useSaveHabit: () => ({ mutate, isPending: false, error: null }),
}));

import { HabitSheet } from './HabitSheet';

beforeEach(() => mutate.mockReset());
afterEach(cleanup);

const pick = (value: string) =>
  fireEvent.change(screen.getByLabelText('Ticked by'), { target: { value } });
const goal = () => screen.getByLabelText(/^Goal/);

describe('HabitSheet', () => {
  it('sends the catalog id, its unit and at most', () => {
    render(<HabitSheet onClose={() => {}} />);
    fireEvent.change(screen.getByLabelText('Name'), { target: { value: 'Calories' } });
    // A habit by hand has no direction to choose.
    expect(screen.queryByRole('radio', { name: 'At most' })).toBeNull();
    pick('food:kcal');
    fireEvent.click(screen.getByRole('radio', { name: 'At most' }));
    fireEvent.change(goal(), { target: { value: '2200' } });
    fireEvent.click(screen.getByRole('button', { name: 'Add' }));
    expect(mutate).toHaveBeenCalledTimes(1);
    expect(mutate.mock.calls[0][0]).toEqual({
      name: 'Calories',
      schedule: 'daily',
      per_week: null,
      goal: 2200,
      unit: 'kcal',
      source: 'food:kcal',
      target: null,
      direction: 'at_most',
    });
  });

  it('sends no goal for a presence source left empty', () => {
    render(<HabitSheet onClose={() => {}} />);
    fireEvent.change(screen.getByLabelText('Name'), { target: { value: 'Weigh-in' } });
    pick('fitness:weight');
    expect(goal().getAttribute('placeholder')).toBe('any record counts');
    expect(screen.getByText(/^Goal \(optional\)/)).toBeTruthy();
    fireEvent.click(screen.getByRole('button', { name: 'Add' }));
    expect(mutate.mock.calls[0][0]).toMatchObject({
      source: 'fitness:weight',
      goal: null,
      direction: 'at_least',
    });
  });

  it('does not offer the habit being edited as its own source', () => {
    const habit = {
      id: 'h1',
      name: 'Water',
      schedule: 'daily',
      per_week: null,
      goal: null,
      unit: 'ml',
      source: 'manual',
      direction: 'at_least',
      target: 2000,
      position: 0,
      archived_at: null,
      created_at: '2026-10-01T00:00:00Z',
    } satisfies Habit;
    render(<HabitSheet habit={habit} onClose={() => {}} />);
    expect(screen.queryByRole('option', { name: 'Water, ml' })).toBeNull();
    expect(screen.getByRole('option', { name: 'Reading, min' })).toBeTruthy();
  });
});

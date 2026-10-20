// @vitest-environment jsdom
import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';

import type { MetricEntry } from '@/lib/metrics';

/** The picker: sections in a fixed order, Manual first, unreadable entries disabled with a reason. */

const entry = (over: Partial<MetricEntry>): MetricEntry => ({
  id: 'fitness:steps',
  module: 'fitness',
  label: 'Steps',
  unit: 'steps',
  reduce: 'sum',
  presence: false,
  available: true,
  why: null,
  ...over,
});

// The catalog as the API could answer it, sections deliberately out of order.
const catalog: MetricEntry[] = [
  entry({ id: 'money:expense:KZT', module: 'money', label: 'Expenses, KZT', unit: 'KZT' }),
  entry({ id: 'food:kcal', module: 'food', label: 'Calories eaten', unit: 'kcal' }),
  entry({}),
  entry({
    id: 'fitness:weight',
    label: 'Weight',
    unit: 'kg',
    presence: true,
    available: false,
    why: 'not_linked',
  }),
  entry({ id: 'habits:h1', module: 'habits', label: 'Water', unit: 'ml', presence: true }),
  entry({ id: 'habits:h2', module: 'habits', label: 'Reading', unit: 'min', presence: true }),
  entry({ id: 'workout:sessions', module: 'workout', label: 'Training sessions', unit: '' }),
];

vi.mock('@/hooks/useMetrics', () => ({
  useMetrics: () => ({ data: catalog, isPending: false }),
}));

import { MetricPicker } from './MetricPicker';

afterEach(cleanup);

const groups = () =>
  Array.from(document.querySelectorAll('optgroup')).map((g) => ({
    label: g.label,
    options: Array.from(g.querySelectorAll('option')).map((o) => o.textContent),
  }));

describe('MetricPicker', () => {
  it('groups by section in the fixed order, Manual first', () => {
    render(<MetricPicker value={null} onChange={() => {}} manual="By hand" />);
    const select = screen.getByRole('combobox') as HTMLSelectElement;
    expect(select.options[0].textContent).toBe('By hand');
    expect(select.value).toBe('');
    expect(groups()).toEqual([
      { label: 'Fitness', options: ['Steps, steps', 'Weight, kg · not linked'] },
      { label: 'Food', options: ['Calories eaten, kcal'] },
      { label: 'Habits', options: ['Water, ml', 'Reading, min'] },
      { label: 'Training', options: ['Training sessions'] },
      { label: 'Money', options: ['Expenses, KZT'] },
    ]);
  });

  it('disables what cannot be read and offers no Manual unless asked', () => {
    render(<MetricPicker value="food:kcal" onChange={() => {}} />);
    const weight = screen.getByRole('option', { name: 'Weight, kg · not linked' });
    expect((weight as HTMLOptionElement).disabled).toBe(true);
    expect(screen.queryByRole('option', { name: 'By hand' })).toBeNull();
  });

  it('keeps the current entry pickable even when it cannot be read now', () => {
    render(<MetricPicker value="fitness:weight" onChange={() => {}} />);
    const weight = screen.getByRole('option', { name: 'Weight, kg · not linked' });
    expect((weight as HTMLOptionElement).disabled).toBe(false);
  });

  it('leaves out the excluded ids and answers null for Manual', () => {
    const onChange = vi.fn();
    render(
      <MetricPicker
        value="food:kcal"
        onChange={onChange}
        manual="By hand"
        exclude={['habits:h1']}
      />,
    );
    expect(screen.queryByRole('option', { name: 'Water, ml' })).toBeNull();
    fireEvent.change(screen.getByRole('combobox'), { target: { value: '' } });
    expect(onChange).toHaveBeenLastCalledWith(null);
    fireEvent.change(screen.getByRole('combobox'), { target: { value: 'habits:h2' } });
    expect(onChange).toHaveBeenLastCalledWith('habits:h2');
  });

  it('keeps a stored id the catalog does not list', () => {
    render(<MetricPicker value="money:expense:EUR" onChange={() => {}} />);
    expect((screen.getByRole('combobox') as HTMLSelectElement).value).toBe('money:expense:EUR');
  });
});

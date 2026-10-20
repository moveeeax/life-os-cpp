// @vitest-environment jsdom
import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

import type { Goal } from '@/lib/goals';
import type { MetricEntry } from '@/lib/metrics';

/** Editing a goal: the PATCH carries only what changed, and Manual is source null. */

const { mutate, saveIds } = vi.hoisted(() => ({ mutate: vi.fn(), saveIds: [] as unknown[] }));

const catalog: MetricEntry[] = [
  {
    id: 'fitness:weight',
    module: 'fitness',
    label: 'Weight',
    unit: 'kg',
    reduce: 'last',
    presence: true,
    available: true,
    why: null,
  },
  {
    id: 'tape:waist',
    module: 'tape',
    label: 'Waist',
    unit: 'cm',
    reduce: 'last',
    presence: true,
    available: true,
    why: null,
  },
];

vi.mock('@/hooks/useMetrics', () => ({
  useMetrics: () => ({ data: catalog, isPending: false }),
}));
vi.mock('@/hooks/useGoals', () => ({
  useSaveGoal: (id: string | null) => {
    saveIds.push(id);
    return { mutate, isPending: false, error: null };
  },
}));

import { GoalForm } from './Goals';

const goal = {
  id: 'g1',
  title: 'Weight 75',
  area: 'health',
  kind: 'number',
  why: '',
  start_date: '2026-10-01',
  due: '2099-12-31',
  status: 'active',
  completed_at: null,
  unit: 'kg',
  start_value: 82,
  target_value: 75,
  target_count: null,
  result: null,
  source: 'fitness:weight',
  source_why: null,
} as unknown as Goal;

beforeEach(() => {
  mutate.mockReset();
  saveIds.length = 0;
});
afterEach(cleanup);

const type = (label: string, value: string) =>
  fireEvent.change(screen.getByLabelText(label), { target: { value } });

describe('GoalForm as an edit', () => {
  it('opens filled and saves the goal by its id with the changed fields only', () => {
    const onClose = vi.fn();
    render(<GoalForm goal={goal} onClose={onClose} />);
    expect((screen.getByLabelText('Title') as HTMLInputElement).value).toBe('Weight 75');
    expect((screen.getByLabelText('Numbers from') as HTMLSelectElement).value).toBe(
      'fitness:weight',
    );
    expect((screen.getByLabelText('Kind') as HTMLSelectElement).disabled).toBe(true);
    expect(saveIds).toContain('g1');
    type('Title', 'Weight 74');
    type('Target', '74');
    fireEvent.click(screen.getByRole('button', { name: 'Save' }));
    expect(mutate).toHaveBeenCalledTimes(1);
    expect(mutate.mock.calls[0][0]).toEqual({ title: 'Weight 74', target_value: 74 });
  });

  it('sends source null when switched to manual check-ins', () => {
    render(<GoalForm goal={goal} onClose={() => {}} />);
    type('Numbers from', '');
    fireEvent.click(screen.getByRole('button', { name: 'Save' }));
    expect(mutate.mock.calls[0][0]).toEqual({ source: null });
  });

  it('closes without a request when nothing changed', () => {
    const onClose = vi.fn();
    render(<GoalForm goal={goal} onClose={onClose} />);
    fireEvent.click(screen.getByRole('button', { name: 'Save' }));
    expect(mutate).not.toHaveBeenCalled();
    expect(onClose).toHaveBeenCalled();
  });
});

describe('GoalForm for a new number goal', () => {
  it('takes the unit of the picked series and sends its id', () => {
    render(<GoalForm onClose={() => {}} />);
    type('Title', 'Waist 80');
    type('By', '2099-06-30');
    type('Numbers from', 'tape:waist');
    expect((screen.getByLabelText('Unit') as HTMLInputElement).value).toBe('cm');
    type('Now', '88');
    type('Target', '80');
    fireEvent.click(screen.getByRole('button', { name: 'Create' }));
    expect(mutate.mock.calls[0][0]).toMatchObject({
      kind: 'number',
      unit: 'cm',
      start_value: 88,
      target_value: 80,
      source: 'tape:waist',
    });
  });
});

// @vitest-environment jsdom
import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

/**
 * The visit sheet: the only person is picked for a new visit, the shared
 * footer submits the trimmed body, and a save error shows as an alert.
 */

const { mutate, save } = vi.hoisted(() => ({
  mutate: vi.fn(),
  save: { error: null as string | null },
}));

vi.mock('@/hooks/useMedical', () => ({
  usePeople: () => ({ data: [{ id: 'p1', name: 'Anna' }], isPending: false }),
  useProviders: () => ({ data: [] }),
  useVisits: () => ({ data: [] }),
  useSaveVisit: () => ({ mutate, isPending: false, error: save.error }),
  useSavePerson: () => ({ mutate, isPending: false, error: null }),
  useSaveProvider: () => ({ mutate, isPending: false, error: null }),
  useSaveRecord: () => ({ mutate, isPending: false, error: null }),
  useSaveMedication: () => ({ mutate, isPending: false, error: null }),
}));

import { VisitSheet } from './Sheets';

beforeEach(() => {
  mutate.mockReset();
  save.error = null;
});
afterEach(cleanup);

describe('VisitSheet', () => {
  it('sends the visit body with the only person', () => {
    render(<VisitSheet onClose={() => {}} />);
    fireEvent.change(screen.getByLabelText('Date'), { target: { value: '2026-11-15' } });
    fireEvent.change(screen.getByLabelText('Reason'), { target: { value: ' Checkup ' } });
    fireEvent.click(screen.getByRole('button', { name: 'Add' }));
    expect(mutate).toHaveBeenCalledWith({
      person_id: 'p1',
      provider_id: null,
      date: '2026-11-15',
      reason: 'Checkup',
      outcome: '',
      follow_up: null,
    });
  });

  it('shows a save error and a working Cancel', () => {
    save.error = 'Nope';
    const onClose = vi.fn();
    render(<VisitSheet onClose={onClose} />);
    expect(screen.getByRole('alert').textContent).toBe('Nope');
    fireEvent.click(screen.getByRole('button', { name: 'Cancel' }));
    expect(onClose).toHaveBeenCalled();
  });
});

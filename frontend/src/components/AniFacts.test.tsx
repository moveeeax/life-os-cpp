// @vitest-environment jsdom
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

import { api } from '@/lib/api/client';

import { AniFacts } from './AniFacts';

/** What Ani knows: listed, edited, deleted and added; muted kinds turn back on. */

beforeEach(() => {
  vi.spyOn(api, 'getJson').mockImplementation(async (path: string) => {
    if (path === '/api/v1/ani/facts')
      return {
        data: [
          {
            id: 'f1',
            text: 'Не любит звонки до 10',
            source: 'chat',
            created_at: '2026-10-29T10:00:00Z',
          },
        ],
      };
    if (path === '/api/v1/ani/deals')
      return { data: [{ id: 'd1', text: 'Зал три раза', kind: 'workouts', target: 3, done: 1 }] };
    return { data: { bare: false, muted: ['stretch', 'brief'] } };
  });
  vi.spyOn(api, 'postJson').mockResolvedValue({ data: { id: 'f2' } });
  vi.spyOn(api, 'patchJson').mockResolvedValue(undefined);
  vi.spyOn(api, 'deleteJson').mockResolvedValue(undefined);
  vi.spyOn(api, 'putJson').mockResolvedValue({ data: { bare: false, muted: ['brief'] } });
});

afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
});

function mount() {
  const qc = new QueryClient({ defaultOptions: { queries: { retry: false } } });
  render(
    <QueryClientProvider client={qc}>
      <AniFacts />
    </QueryClientProvider>,
  );
}

describe('AniFacts', () => {
  it('lists, edits and deletes a fact', async () => {
    mount();
    fireEvent.click(await screen.findByText(/What Ani knows about you \(1\)/));
    expect(screen.getByText('Не любит звонки до 10')).toBeTruthy();
    fireEvent.click(screen.getByText('Edit'));
    fireEvent.change(screen.getByLabelText('The fact'), { target: { value: 'Не звонить до 11' } });
    fireEvent.click(screen.getByText('Save'));
    await waitFor(() =>
      expect(api.patchJson).toHaveBeenCalledWith('/api/v1/ani/facts/f1', {
        body: { text: 'Не звонить до 11' },
      }),
    );
    fireEvent.click(screen.getByText('Delete'));
    await waitFor(() => expect(api.deleteJson).toHaveBeenCalledWith('/api/v1/ani/facts/f1'));
  });

  it('adds a fact and turns a muted kind back on', async () => {
    mount();
    fireEvent.click(await screen.findByText(/What Ani knows/));
    fireEvent.change(screen.getByLabelText('A new fact'), {
      target: { value: 'Живет в поездках' },
    });
    fireEvent.click(screen.getByText('Add'));
    await waitFor(() =>
      expect(api.postJson).toHaveBeenCalledWith('/api/v1/ani/facts', {
        body: { text: 'Живет в поездках' },
      }),
    );
    expect(screen.getByText('Stretch reminders')).toBeTruthy();
    fireEvent.click(screen.getAllByText('Turn back on')[0]);
    await waitFor(() =>
      expect(api.putJson).toHaveBeenCalledWith('/api/v1/ani/prefs', { body: { muted: ['brief'] } }),
    );
  });

  it('PromisesOfTheWeek: listed with their progress and dropped', async () => {
    mount();
    expect(await screen.findByText('Зал три раза')).toBeTruthy();
    expect(screen.getByText('1 of 3')).toBeTruthy();
    fireEvent.click(screen.getByText('Drop'));
    await waitFor(() => expect(api.deleteJson).toHaveBeenCalledWith('/api/v1/ani/deals/d1'));
  });
});

// @vitest-environment jsdom
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';

import { api } from '@/lib/api/client';

import { Board } from './Board';

afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
});

function mount(kind: 'water' | 'protein') {
  vi.spyOn(api, 'getJson').mockImplementation(async (path: string) => {
    if (path === '/api/v1/food/water')
      return { data: { date: '2026-10-31', ml: 1000, goal_ml: 2000 } };
    if (path === '/api/v1/food/day')
      return { data: { totals: { protein_g: 62.4 }, targets: { protein_g: 140 } } };
    if (path === '/api/v1/food/recent')
      return {
        data: [
          { id: 'skyr', name: 'Skyr', protein_g: 11, servings: [{ label: 'cup', grams: 150 }] },
        ],
      };
    return { data: [] };
  });
  const post = vi
    .spyOn(api, 'postJson')
    .mockResolvedValue({ data: { date: '2026-10-31', ml: 1250, goal_ml: 2000 } });
  const qc = new QueryClient({ defaultOptions: { queries: { retry: false } } });
  render(
    <QueryClientProvider client={qc}>
      <Board widgets={[{ kind }]} today={undefined} date="2026-10-31" tz="Asia/Tashkent" />
    </QueryClientProvider>,
  );
  return post;
}

describe('Board', () => {
  it('WaterGlassInOneTap: a glass adds 250 ml to the day', async () => {
    const post = mount('water');
    expect(await screen.findByText('1 of 2 l')).toBeTruthy();
    fireEvent.click(screen.getByText('+ 250 ml'));
    await waitFor(() =>
      expect(post).toHaveBeenCalledWith('/api/v1/food/water', {
        body: { date: '2026-10-31', add_ml: 250 },
      }),
    );
    expect(await screen.findByText('1.25 of 2 l')).toBeTruthy();
  });

  it('ProteinInOneTap: a recent rich product goes to the diary with its serving', async () => {
    const post = mount('protein');
    expect(await screen.findByText('62 of 140 g')).toBeTruthy();
    fireEvent.click(await screen.findByText('+ Skyr'));
    await waitFor(() =>
      expect(post).toHaveBeenCalledWith('/api/v1/food/entries', {
        body: expect.objectContaining({ date: '2026-10-31', item_id: 'skyr', grams: 150 }),
      }),
    );
  });
});

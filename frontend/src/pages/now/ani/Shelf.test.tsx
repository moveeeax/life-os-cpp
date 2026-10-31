// @vitest-environment jsdom
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { act, cleanup, render, screen } from '@testing-library/react';
import { MemoryRouter } from 'react-router';
import { afterEach, describe, expect, it, vi } from 'vitest';

import { api } from '@/lib/api/client';

import { Shelf } from './Shelf';

vi.mock('@/hooks/useTravel', () => ({
  useCrossings: () => ({ data: [{ date: '2026-10-24', to_country: 'UZ' }] }),
}));

afterEach(() => {
  cleanup();
  vi.useRealTimers();
  vi.restoreAllMocks();
});

describe('Shelf', () => {
  it('ShelfFromData: a plant by the streak, a book, a cup, the morning coffee and a postcard', async () => {
    vi.useFakeTimers({ shouldAdvanceTime: true });
    vi.spyOn(api, 'getJson').mockImplementation(async (path: string) => {
      if (path === '/api/v1/habits/days')
        return { data: [{ schedule: 'daily', streak: 12, best: 12, days: [] }] };
      if (path === '/api/v1/tasks/projects') return { data: [{ id: 'c1' }] };
      if (path === '/api/v1/ani/deals') return { data: [{ done: 3, target: 3 }] };
      return { data: [] };
    });
    const qc = new QueryClient({ defaultOptions: { queries: { retry: false } } });
    render(
      <QueryClientProvider client={qc}>
        <MemoryRouter>
          <Shelf on date="2026-11-01" sleepMinutes={420} hour={8} />
        </MemoryRouter>
      </QueryClientProvider>,
    );
    await act(async () => {
      await vi.advanceTimersByTimeAsync(200_000);
    });
    expect(await screen.findByTitle('Longest habit streak: 12 days')).toBeTruthy();
    expect(screen.getByTitle('Courses finished: 1')).toBeTruthy();
    expect(screen.getByTitle('A promise kept this week')).toBeTruthy();
    expect(screen.getByTitle('Slept well')).toBeTruthy();
    expect(screen.getByTitle('Last crossing: UZ, 2026-10-24')).toBeTruthy();
  });
});

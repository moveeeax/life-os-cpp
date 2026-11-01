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

function mountShelf(hidden = false) {
  vi.useFakeTimers({ shouldAdvanceTime: true });
  const get = vi.spyOn(api, 'getJson').mockImplementation(async (path: string) => {
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
        <Shelf on date="2026-11-01" tz="Asia/Almaty" sleepMinutes={420} hour={8} hidden={hidden} />
      </MemoryRouter>
    </QueryClientProvider>,
  );
  return { qc, get };
}

describe('Shelf', () => {
  it('ShelfFromData: a plant by the streak, a book, a cup, the morning coffee and a postcard', async () => {
    const { qc, get } = mountShelf();
    await act(async () => {
      await vi.advanceTimersByTimeAsync(200_000);
    });
    expect(await screen.findByTitle('Longest habit streak: 12 days')).toBeTruthy();
    expect(screen.getByTitle('Courses finished: 1')).toBeTruthy();
    expect(screen.getByTitle('A promise kept this week')).toBeTruthy();
    expect(screen.getByTitle('Slept well')).toBeTruthy();
    expect(screen.getByTitle('Last crossing: UZ, 2026-10-24')).toBeTruthy();
    // The keys the rest of the site reads these under: a write there reaches the shelf.
    expect(qc.getQueryData(['ani', 'deals', 'Asia/Almaty'])).toEqual([{ done: 3, target: 3 }]);
    expect(get).toHaveBeenCalledWith(
      '/api/v1/ani/deals',
      expect.objectContaining({ query: { tz: 'Asia/Almaty' } }),
    );
    expect(get).toHaveBeenCalledWith(
      '/api/v1/habits/days',
      expect.objectContaining({ query: expect.objectContaining({ tz: expect.any(String) }) }),
    );
  });

  it('ShelfHideNumbers: the tooltips carry no counts or dates under Hide numbers', async () => {
    mountShelf(true);
    await act(async () => {
      await vi.advanceTimersByTimeAsync(200_000);
    });
    expect(await screen.findByTitle('Longest habit streak')).toBeTruthy();
    expect(screen.getByTitle('Courses finished')).toBeTruthy();
    expect(screen.getByTitle('Last crossing: UZ')).toBeTruthy();
    for (const el of document.querySelectorAll('[title]'))
      expect(el.getAttribute('title')).not.toMatch(/\d/);
  });
});

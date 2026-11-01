// @vitest-environment jsdom
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { act, cleanup, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';

import { api } from '@/lib/api/client';

import { Ribbon } from './Ribbon';

const NOW = Date.UTC(2026, 10, 1, 9, 0);
const free = {
  kind: 'free' as const,
  key: 'f1',
  title: 'Free',
  left: 40,
  width: 20,
  start: NOW,
  end: NOW + 2 * 3_600_000,
};

// A day with the calendar linked and one free window; a tap on the track lands in it.
vi.mock('./layout', async (orig) => ({
  ...(await orig<typeof import('./layout')>()),
  useNowDay: () => ({
    data: { calendar: 'ok', events: [], free: [], focus: [], workouts: [], tasks: [] },
  }),
  blocksOf: () => [free],
  pinsOf: () => [],
  nextEvent: () => null,
  freeAt: () => free,
}));

afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
});

function show() {
  render(
    <QueryClientProvider client={new QueryClient()}>
      <Ribbon date="2026-11-01" tz="UTC" now={NOW} onFocus={() => {}} />
    </QueryClientProvider>,
  );
  fireEvent.click(document.querySelector('.r-track')!);
}

describe('Ribbon', () => {
  it('FocusBlockOnce: a double click on "Add a focus block" makes one calendar event', async () => {
    let made: (v: { data: object }) => void = () => {};
    const post = vi
      .spyOn(api, 'postJson')
      .mockImplementation(() => new Promise((ok) => (made = ok)));
    show();
    fireEvent.click(screen.getByRole('button', { name: 'Add a focus block' }));
    fireEvent.click(screen.getByRole('button', { name: 'Adding…' }));
    expect(post).toHaveBeenCalledTimes(1);
    await act(async () => made({ data: {} }));
    expect(screen.queryByRole('dialog', { name: 'Free time' })).toBeNull();
  });

  it('FocusBlockError: a failed block keeps the menu open with a line', async () => {
    vi.spyOn(api, 'postJson').mockRejectedValue(new Error('offline'));
    show();
    await act(async () => {
      fireEvent.click(screen.getByRole('button', { name: 'Add a focus block' }));
    });
    expect(screen.getByRole('dialog', { name: 'Free time' })).toBeTruthy();
    expect(screen.getByRole('alert').textContent).toBe('The block was not added. Try again.');
  });
});

// @vitest-environment jsdom
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { cleanup, fireEvent, render, screen, within } from '@testing-library/react';
import { MemoryRouter } from 'react-router';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

import { api } from '@/lib/api/client';

import { TravelTripsPage } from './Trips';

/** Trips: a year at a time, a strip of its trips, the table rows tied to the strip. */

const trip = (id: string, name: string, start_date: string, end_date: string, c: string) => ({
  id,
  name,
  start_date,
  end_date,
  countries: [c],
  note: '',
  purpose: null,
  booked_cost: null,
  packing: { done: 0, total: 0 },
  spends: {
    by_currency: [],
    total: { currency: 'USD', amount: 0, partial: false, rate_date: null },
    count: 0,
  },
});

beforeEach(() => {
  vi.useFakeTimers({ toFake: ['Date'] });
  vi.setSystemTime(new Date('2026-10-29T09:00:00'));
  localStorage.setItem('travel:trips:view', 'table');
  vi.spyOn(api, 'getJson').mockImplementation(async (path: string) => {
    if (path.endsWith('/travel/trips'))
      return {
        data: [
          trip('1', 'Tashkent (Oct 2026)', '2026-10-24', '2026-11-20', 'UZ'),
          trip('2', 'St Petersburg (Oct 2026)', '2026-10-17', '2026-10-17', 'RU'),
          trip('3', 'Phuket', '2026-09-12', '2026-10-10', 'TH'),
          trip('4', 'Tbilisi', '2025-11-04', '2025-11-18', 'GE'),
        ],
      };
    return { data: [] };
  });
});

afterEach(() => {
  cleanup();
  vi.useRealTimers();
  vi.restoreAllMocks();
  localStorage.clear();
});

function mount() {
  const qc = new QueryClient({ defaultOptions: { queries: { retry: false } } });
  render(
    <QueryClientProvider client={qc}>
      <MemoryRouter initialEntries={['/travel']}>
        <TravelTripsPage />
      </MemoryRouter>
    </QueryClientProvider>,
  );
}

describe('TravelTripsPage', () => {
  it('shows this year with one line per trip, newest first', async () => {
    mount();
    expect(
      await screen.findByText('Tashkent (Oct 2026)', { selector: '.k-row-title' }),
    ).toBeTruthy();
    const titles = [...document.querySelectorAll('.k-row-title')].map((e) => e.textContent);
    expect(titles).toEqual(['Tashkent (Oct 2026)', 'St Petersburg (Oct 2026)', 'Phuket']);
    expect(screen.getByText(/now · Oct 24 – Nov 20 · 5 of 27 d/)).toBeTruthy();
    expect(screen.getByText(/Oct 17 · day trip/)).toBeTruthy();
    expect(screen.getByText('2 trips · 28 days away')).toBeTruthy();
  });

  it('switches the year', async () => {
    mount();
    await screen.findByText('Phuket', { selector: '.k-row-title' });
    fireEvent.click(within(screen.getByRole('group', { name: 'Year' })).getByText('2025'));
    expect(screen.getByText('Tbilisi', { selector: '.k-row-title' })).toBeTruthy();
    expect(screen.queryByText('Phuket', { selector: '.k-row-title' })).toBeNull();
  });

  it('a strip segment opens its row and the row marks the segment', async () => {
    mount();
    await screen.findByText('Phuket', { selector: '.k-row-title' });
    const seg = screen.getByRole('button', { name: /^Phuket, / });
    fireEvent.click(seg);
    expect(seg.getAttribute('aria-pressed')).toBe('true');
    expect(screen.getByText('Open the trip')).toBeTruthy();
    fireEvent.click(screen.getByText('Tashkent (Oct 2026)', { selector: '.k-row-title' }));
    expect(
      screen.getByRole('button', { name: /^Tashkent \(Oct 2026\), / }).getAttribute('aria-pressed'),
    ).toBe('true');
    expect(seg.getAttribute('aria-pressed')).toBe('false');
  });
});

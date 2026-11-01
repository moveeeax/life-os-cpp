// @vitest-environment jsdom
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { act, cleanup, renderHook } from '@testing-library/react';
import type { ReactNode } from 'react';
import { MemoryRouter } from 'react-router';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

import { api } from '@/lib/api/client';

import type { Answer } from './touch';
import { useLife } from './useLife';
import { useLife2 } from './useLife2';

vi.mock('@/hooks/useTravel', () => ({ useCrossings: () => ({ data: [] }) }));
vi.mock('@/hooks/useMoney', async (orig) => ({
  ...(await orig<typeof import('@/hooks/useMoney')>()),
  useMoneySettings: () => ({ data: { view_currency: 'KZT' } }),
}));

// Asia/Almaty is UTC+5: local 08:00 on 2026-11-01 is 03:00 UTC.
const TZ = 'Asia/Almaty';
const DATE = '2026-11-01';
const atLocal = (h: number, m = 0) => new Date(Date.UTC(2026, 10, 1, h - 5, m));

// Five nights that started at 23:00 local and last night, which started at 01:00.
const night = (day: number, startUtcHour: number, endUtcHour: number) => ({
  start_at: new Date(Date.UTC(2026, 9, day, startUtcHour)).toISOString(),
  end_at: new Date(Date.UTC(2026, 9, day + 1, endUtcHour)).toISOString(),
  duration_minutes: 420,
  is_nap: false,
});
const SLEEPS = [
  night(25, 18, 1),
  night(26, 18, 1),
  night(27, 18, 1),
  night(28, 18, 1),
  night(29, 18, 1),
  { ...night(31, 20, 2), start_at: new Date(Date.UTC(2026, 9, 31, 20)).toISOString() },
];
const LATE = 'Вчера сон начался на 2 ч позже обычного. Сегодня давай пораньше.';

/** What each GET answers; the sleep read can be held back to arrive late. */
let answers: Record<string, unknown> = {};
let sleepHeld: Promise<void> | null = null;

function wrapper(path: string) {
  const qc = new QueryClient({ defaultOptions: { queries: { retry: false } } });
  return ({ children }: { children: ReactNode }) => (
    <QueryClientProvider client={qc}>
      <MemoryRouter initialEntries={[path]}>{children}</MemoryRouter>
    </QueryClientProvider>
  );
}

function run(hook: typeof useLife2 | typeof useLife, path = '/now', today = undefined) {
  const said: Answer[] = [];
  const onChanged = vi.fn();
  renderHook(
    () =>
      hook({
        on: true,
        context: { tz: TZ, date: DATE, today },
        engine: { current: null },
        quiet: () => false,
        say: (a) => said.push(a),
        onSeed: () => {},
        onChanged,
      }),
    { wrapper: wrapper(path) },
  );
  return { said, onChanged };
}

/** @p n minutes, ten seconds per act: React renders between the steps (a warm read turns on). */
async function minutes(n: number) {
  for (let i = 0; i < n * 6; i++)
    await act(async () => {
      await vi.advanceTimersByTimeAsync(10_000);
    });
}

beforeEach(() => {
  localStorage.clear();
  document.documentElement.classList.remove('hide-numbers');
  answers = { '/api/v1/fitness/sleep': SLEEPS };
  sleepHeld = null;
  vi.spyOn(api, 'getJson').mockImplementation(async (path: string) => {
    if (path === '/api/v1/fitness/sleep' && sleepHeld) await sleepHeld;
    return { data: answers[path] ?? [] };
  });
});
afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
  vi.useRealTimers();
});

describe('useLife2', () => {
  it('BedtimeWaitsForSleep: the morning bedtime line is not lost when the sleep read comes late', async () => {
    vi.useFakeTimers({ now: atLocal(8) });
    let release = () => {};
    sleepHeld = new Promise<void>((ok) => (release = ok));
    const { said } = run(useLife2);
    // The first ticks run before the night is in: nothing said, nothing marked.
    await minutes(3);
    expect(said.map((a) => a.text)).not.toContain(LATE);
    release();
    await minutes(2);
    expect(said.map((a) => a.text)).toContain(LATE);
  });

  it('MorningWindow: right after midnight the morning lines wait for 05:00', async () => {
    vi.useFakeTimers({ now: atLocal(0, 30) });
    const { said } = run(useLife2);
    await minutes(10);
    expect(said.map((a) => a.text)).not.toContain(LATE);
    vi.setSystemTime(atLocal(6));
    await minutes(2);
    expect(said.map((a) => a.text)).toContain(LATE);
  });

  it('HideNumbersTab: the month spend on Money waits while Hide numbers is on', async () => {
    vi.useFakeTimers({ now: atLocal(15) });
    answers['/api/v1/money/reports/period'] = { as_if: { expense: 345_000, currency: 'KZT' } };
    document.documentElement.classList.add('hide-numbers');
    const { said } = run(useLife2, '/money');
    await minutes(8);
    expect(said.some((a) => /\d/.test(a.text))).toBe(false);
    document.documentElement.classList.remove('hide-numbers');
    await minutes(2);
    expect(said.map((a) => a.text)).toContainEqual(
      expect.stringMatching(/ушло около 345\s000 KZT/),
    );
  });

  it('HideNumbersTab: no line of hers under Hide numbers carries a digit', async () => {
    vi.useFakeTimers({ now: atLocal(8) });
    answers['/api/v1/metrics/series'] = Array.from({ length: 10 }, (_, i) => ({
      date: `2026-10-${String(22 + i).padStart(2, '0')}`,
      value: i === 9 ? 20_000 : 5_000,
    }));
    document.documentElement.classList.add('hide-numbers');
    const { said } = run(useLife2);
    await minutes(15);
    for (const a of said) expect(a.text).not.toMatch(/\d/);
  });

  it('MealChipRefreshes: a meal she logs refreshes the food diary', async () => {
    vi.useFakeTimers({ now: atLocal(13, 1) });
    answers['/api/v1/food/day'] = {
      totals: { kcal: 600, protein_g: 40 },
      targets: { kcal: 2200, protein_g: 140 },
    };
    answers['/api/v1/food/recent'] = [
      { id: 'skyr', name: 'Skyr', kcal: 60, protein_g: 11, servings: [{ grams: 150 }] },
    ];
    vi.spyOn(api, 'postJson').mockResolvedValue({ data: {} });
    const { said, onChanged } = run(useLife2);
    await minutes(5);
    const meal = said.find((a) => a.chips?.length);
    expect(meal?.text).toMatch(/До цели 100 г белка/);
    await act(async () => {
      meal!.chips![0].run();
      await vi.advanceTimersByTimeAsync(10);
    });
    expect(onChanged).toHaveBeenCalledWith('food');
  });
});

describe('useLife', () => {
  it('EnergyMorningWindow: the night energy line waits for 05:00', async () => {
    vi.useFakeTimers({ now: atLocal(0, 30) });
    const today = { health: { sleep_minutes: 300 } } as never;
    const { said } = run(useLife, '/now', today);
    await minutes(5);
    expect(said.map((a) => a.kind)).not.toContain('brief');
    vi.setSystemTime(atLocal(6));
    await minutes(2);
    expect(said.map((a) => a.kind)).toContain('brief');
  });
});

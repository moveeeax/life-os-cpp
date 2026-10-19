// @vitest-environment jsdom
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { cleanup, render } from '@testing-library/react';
import type { ReactElement } from 'react';
import { MemoryRouter } from 'react-router';
import { afterEach, describe, expect, it, vi } from 'vitest';

import { ToastProvider } from '@/components/ui/toaster';
import type { Account, BalanceGroup, Currency } from '@/lib/ledger/types';
import type { components } from '@/lib/api/schema.gen';
import type {
  BodyMeasurement,
  DailyActivity,
  DaySummary,
  Spo2Sample,
  Workout,
} from '@/lib/health/types';

/**
 * The Hide numbers guard: with the mask on, every text that holds a digit on
 * the main number pages must be blurred (.num, .num-chart, .k-review) or say
 * it is not personal (data-num-ok). A new money or health number left
 * unmarked fails here instead of showing through the mask.
 */

const ok = <T,>(data: T) => ({
  data,
  isPending: false,
  isError: false,
  isSuccess: true,
  error: null,
  refetch: vi.fn(),
});

// ── money fixtures ──────────────────────────────────────────────────────

const account = (over: Partial<Account>): Account => ({
  id: 'a1',
  name: 'Kaspi Gold',
  bank: 'Kaspi',
  kind: 'card',
  currency: 'KZT',
  last4: '4417',
  opening_balance: 120_000,
  opening_date: '2026-01-01',
  archived: false,
  position: 0,
  balance: 352_480.5,
  last_activity: '2026-10-17',
  created_at: '2026-01-01T00:00:00Z',
  updated_at: '2026-10-17T09:00:00Z',
  ...over,
});

const accounts: Account[] = [
  account({}),
  account({
    id: 'a2',
    name: 'Wise USD',
    bank: 'Wise',
    currency: 'USD',
    last4: '0921',
    balance: 2_140.27,
  }),
  account({ id: 'a3', name: 'Old deposit', archived: true, currency: 'USD', balance: 12.5 }),
];

const balances: BalanceGroup[] = [
  { currency: 'KZT', total: 352_480.5, accounts: [accounts[0]] },
  { currency: 'USD', total: 2_140.27, accounts: [accounts[1]] },
];

const currencies: Currency[] = [
  { code: 'KZT', name: 'Tenge', role: 'local', decimals: 2, minor_unit: 'tiyn', archived: false },
  {
    code: 'USD',
    name: 'US dollar',
    role: 'primary',
    decimals: 2,
    minor_unit: 'cent',
    archived: false,
  },
];

const summary: components['schemas']['MoneySummary'] = {
  currency: 'USD',
  date: '2026-10-18',
  total: 2_874.31,
  partial: false,
  rate_date: '2026-10-17',
  accounts: [
    {
      id: 'a1',
      name: 'Kaspi Gold',
      kind: 'card',
      currency: 'KZT',
      balance: 352_480.5,
      rate: 0.00208,
      rate_date: '2026-10-17',
      value: 734.04,
    },
    {
      id: 'a2',
      name: 'Wise USD',
      kind: 'card',
      currency: 'USD',
      balance: 2_140.27,
      rate: 1,
      rate_date: '2026-10-17',
      value: 2_140.27,
    },
  ],
  not_converted: [{ currency: 'GEL', amount: 45.5 }],
  change: { from: '2026-07-20', total: 312.4, ledger: 280.1, rates: 32.3, partial: false },
  days: [
    { date: '2026-10-16', total: 2_801.2, partial: false },
    { date: '2026-10-17', total: 2_850.9, partial: false },
    { date: '2026-10-18', total: 2_874.31, partial: false },
  ],
};

vi.mock('@/hooks/useMoney', async () => {
  const actual = await vi.importActual<typeof import('@/hooks/useMoney')>('@/hooks/useMoney');
  return {
    ...actual,
    useBalances: () => ok(balances),
    useMoneySummary: () => ok(summary),
    useAccounts: (archived?: boolean) =>
      ok(archived ? accounts : accounts.filter((a) => !a.archived)),
    useCurrencies: () => ok(currencies),
  };
});

// ── health fixtures ─────────────────────────────────────────────────────

const days = ['2026-10-12', '2026-10-13', '2026-10-14', '2026-10-15', '2026-10-16'];
const daySummary: DaySummary[] = days.map((date, i) => ({
  date,
  steps: 8_412 + i * 311,
  distance_m: 6_120 + i * 90,
  active_kcal: 412 + i,
  sleep_duration_minutes: 431 + i * 7,
  sleep_score: 78 + i,
  resting_bpm: 57 + (i % 3),
}));
const activity: DailyActivity[] = daySummary.map((d) => ({
  date: d.date,
  steps: d.steps,
  distance_m: d.distance_m,
  active_kcal: d.active_kcal,
  total_kcal: 2_310,
  timezone: 'Asia/Almaty',
}));
const body: BodyMeasurement[] = [
  { timestamp: '2026-10-13T07:10:00Z', weight_kg: 81.4 },
  { timestamp: '2026-10-16T07:05:00Z', weight_kg: 80.9 },
].map((r) => ({
  ...r,
  bmi: 24.1,
  body_fat_pct: 18.2,
  muscle_mass_kg: 63.1,
  water_pct: 55.4,
  bone_mass_kg: 3.2,
  visceral_fat_score: 8,
  basal_metabolism_kcal: 1_780,
  metabolic_age: 31,
}));
const workouts: Workout[] = [
  {
    workout_id: 'w1',
    activity_type: 'outdoor_run',
    start_at: '2026-10-14T13:30:00Z',
    end_at: '2026-10-14T14:12:00Z',
    duration_minutes: 42,
    distance_m: 7_340,
    calories_kcal: 512,
    avg_heart_rate_bpm: 148,
    max_heart_rate_bpm: 171,
    avg_pace_sec_per_km: 343,
    max_pace_sec_per_km: 298,
    total_steps: 6_900,
  },
  {
    workout_id: 'w2',
    activity_type: 'strength',
    start_at: '2026-10-15T16:00:00Z',
    end_at: '2026-10-15T16:55:00Z',
    duration_minutes: 55,
    distance_m: null,
    calories_kcal: 310,
    avg_heart_rate_bpm: null,
    max_heart_rate_bpm: null,
    avg_pace_sec_per_km: null,
    max_pace_sec_per_km: null,
    total_steps: null,
  },
];
const spo2: Spo2Sample[] = [{ timestamp: '2026-10-15T03:00:00Z', spo2_pct: 97 }];

vi.mock('@/hooks/useHealth', async () => {
  const actual = await vi.importActual<typeof import('@/hooks/useHealth')>('@/hooks/useHealth');
  return {
    ...actual,
    useSummary: () => ok(daySummary),
    useActivity: () => ok(activity),
    useSleep: () => ok([]),
    useHeartRate: () => ok([]),
    useStress: () => ok([]),
    useSpo2: () => ok(spo2),
    useBody: () => ok(body),
    useWorkouts: () => ok(workouts),
  };
});

vi.mock('@/hooks/useMe', () => ({
  useMe: () =>
    ok({
      id: 'u1',
      email: 'me@example.com',
      full_name: 'Me',
      confirmed: true,
      role_id: 2,
      // Fitness read only: the sync panel (fitness:sync) stays out.
      role: { id: 2, name: 'reader', permissions: 0x04 },
    }),
}));

vi.mock('@/hooks/useMiAccount', () => ({
  useMiAccount: () => ok({ status: 'ok' }),
}));

// The chart library stays out: the chart draws into .num-chart, blurred as a whole.
vi.mock('./health/LazyTimeChart', () => ({
  TimeChart: () => <div className="num-chart" />,
}));

import { HealthPage } from './Health';
import { MoneyAccountsPage } from './money/Accounts';
import { MoneySummaryPage } from './money/Summary';

const MARKED = '.num, .num-chart, .k-review, [data-num-ok]';

/** Every text with a digit that neither blurs nor says it is not personal. */
function unmarked(root: HTMLElement): string[] {
  const out: string[] = [];
  const walk = document.createTreeWalker(root, NodeFilter.SHOW_TEXT);
  for (let n = walk.nextNode(); n; n = walk.nextNode()) {
    const text = n.textContent ?? '';
    if (!/\d/.test(text)) continue;
    const parent = n.parentElement;
    // Option texts are drawn by the browser's own picker, not the page.
    if (!parent || parent.closest('option, script, style')) continue;
    if (!parent.closest(MARKED)) out.push(`${parent.tagName.toLowerCase()}: ${text.trim()}`);
  }
  return out;
}

function mount(page: ReactElement, path: string) {
  const qc = new QueryClient({ defaultOptions: { queries: { retry: false } } });
  render(
    <QueryClientProvider client={qc}>
      <ToastProvider>
        <MemoryRouter initialEntries={[path]}>{page}</MemoryRouter>
      </ToastProvider>
    </QueryClientProvider>,
  );
}

afterEach(cleanup);

describe('Hide numbers covers every number', () => {
  it.each([
    ['money accounts', <MoneyAccountsPage key="a" />, '/money/accounts'],
    ['money summary', <MoneySummaryPage key="s" />, '/money/summary'],
    ['health', <HealthPage key="h" />, '/health?from=2026-10-12&to=2026-10-16'],
  ] as const)('%s', (_name, page, path) => {
    mount(page, path);
    // The fixtures did render: a page that shows nothing passes for nothing.
    expect(document.body.textContent).toMatch(/\d/);
    expect(unmarked(document.body)).toEqual([]);
  });
});

// @vitest-environment jsdom
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import type { ReactElement } from 'react';
import { MemoryRouter, Route, Routes } from 'react-router';
import { afterEach, describe, expect, it, vi } from 'vitest';

import { ToastProvider } from '@/components/ui/toaster';
import type {
  PracticeClient,
  PracticeDeal,
  PracticeEntry,
  PracticeMonths,
  PracticeMonthView,
} from '@/lib/practice';

import { PracticeClientsPage } from './Clients';
import { PracticeDealsPage } from './Deals';
import { PracticeMoneyPage } from './Money';
import { PracticeMonthPage } from './Month';
import { PracticeMonthsPage } from './Months';

/**
 * The Hide numbers guard of Practice: with the fixtures below, every text
 * that holds a digit is blurred (.num) or says it is not personal
 * (data-num-ok: a day, a month). The amounts are the owner's money.
 */

const ok = <T,>(data: T) => ({
  data,
  isPending: false,
  isError: false,
  isSuccess: true,
  error: null,
  refetch: vi.fn(),
});

const mutation = () => ({ mutate: vi.fn(), isPending: false, error: null });

const client: PracticeClient = {
  id: 'c1',
  name: 'Acme',
  contact: '+7 701 555 0101',
  note: '',
  open_deals: 2,
  income: [
    { currency: 'USD', amount: 1000 },
    { currency: 'KZT', amount: 250000 },
  ],
  created_at: '2026-10-01T00:00:00+00:00',
  updated_at: '2026-10-01T00:00:00+00:00',
};

const deal: PracticeDeal = {
  id: 'd1',
  client_id: 'c1',
  client_name: 'Acme',
  title: 'Landing page',
  stage: 'active',
  value: 1500,
  currency: 'USD',
  start_date: '2026-10-01',
  end_date: '2026-11-30',
  note: '',
  open_tasks: 3,
  received: [{ currency: 'USD', amount: 1000 }],
  created_at: '2026-10-01T00:00:00+00:00',
  updated_at: '2026-10-01T00:00:00+00:00',
};

const entry: PracticeEntry = {
  id: 'e1',
  kind: 'income',
  date: '2026-10-03',
  amount: 1000,
  currency: 'USD',
  client_id: 'c1',
  client_name: 'Acme',
  deal_id: 'd1',
  deal_title: 'Landing page',
  note: 'First half',
  created_at: '2026-10-03T00:00:00+00:00',
  updated_at: '2026-10-03T00:00:00+00:00',
};

const expense: PracticeEntry = {
  ...entry,
  id: 'e2',
  kind: 'expense',
  date: '2026-09-20',
  amount: 12.5,
  currency: 'EUR',
  client_id: null,
  client_name: null,
  deal_id: null,
  deal_title: null,
  note: 'Hosting',
};

const totals = [
  { currency: 'KZT', income: 250000, expense: 0, net: 250000, entries: 1 },
  { currency: 'USD', income: 1000, expense: 20, net: 980, entries: 2 },
];

const months: PracticeMonths = {
  year: 2026,
  data: [{ month: '2026-10', entries: 3, totals }],
  totals,
};

const month: PracticeMonthView = {
  month: '2026-10',
  totals,
  by_client: [
    {
      client_id: 'c1',
      client_name: 'Acme',
      currency: 'USD',
      income: 1000,
      expense: 0,
      net: 1000,
      entries: 1,
    },
    {
      client_id: null,
      client_name: null,
      currency: 'USD',
      income: 0,
      expense: 20,
      net: -20,
      entries: 1,
    },
  ],
  entries: [entry],
};

vi.mock('@/hooks/usePractice', () => ({
  useClients: () => ok([client]),
  useDeals: () => ok([deal]),
  useEntries: () => ok({ data: [entry, expense], total: 2 }),
  useMonths: () => ok(months),
  useMonth: () => ok(month),
  useDealTasks: () => ok([]),
  useSaveClient: () => mutation(),
  useDeleteClient: () => mutation(),
  useSaveDeal: () => mutation(),
  useDeleteDeal: () => mutation(),
  useSaveEntry: () => mutation(),
  useDeleteEntry: () => mutation(),
  useAddNextStep: () => mutation(),
}));

const MARKED = '.num, [data-num-ok]';

function unmarked(root: HTMLElement): string[] {
  const out: string[] = [];
  const walk = document.createTreeWalker(root, NodeFilter.SHOW_TEXT);
  for (let n = walk.nextNode(); n; n = walk.nextNode()) {
    const text = n.textContent ?? '';
    if (!/\d/.test(text)) continue;
    const parent = n.parentElement;
    if (!parent || parent.closest('option, script, style')) continue;
    if (!parent.closest(MARKED)) out.push(`${parent.tagName.toLowerCase()}: ${text.trim()}`);
  }
  return out;
}

function mount(page: ReactElement, path: string, pattern = path) {
  const qc = new QueryClient({ defaultOptions: { queries: { retry: false } } });
  render(
    <QueryClientProvider client={qc}>
      <ToastProvider>
        <MemoryRouter initialEntries={[path]}>
          <Routes>
            <Route path={pattern} element={page} />
          </Routes>
        </MemoryRouter>
      </ToastProvider>
    </QueryClientProvider>,
  );
}

/** Open every row, so the details are checked too. */
function openRows() {
  for (const b of screen.queryAllByRole('button', { expanded: false })) fireEvent.click(b);
}

afterEach(cleanup);

describe('Practice: Hide numbers covers every number', () => {
  it.each([
    ['deals', <PracticeDealsPage key="d" />, '/practice', '/practice'],
    ['money', <PracticeMoneyPage key="m" />, '/practice/money', '/practice/money'],
    ['months', <PracticeMonthsPage key="ms" />, '/practice/months', '/practice/months'],
    [
      'month',
      <PracticeMonthPage key="mo" />,
      '/practice/months/2026-10',
      '/practice/months/:month',
    ],
    ['clients', <PracticeClientsPage key="c" />, '/practice/clients', '/practice/clients'],
  ] as const)('%s', (_name, page, path, pattern) => {
    mount(page, path, pattern);
    openRows();
    // The fixtures did render: a page that shows nothing passes for nothing.
    expect(document.body.textContent).toMatch(/\d/);
    expect(unmarked(document.body)).toEqual([]);
  });

  it('shows each currency apart, never one sum', () => {
    mount(<PracticeMonthsPage />, '/practice/months');
    const text = document.body.textContent ?? '';
    expect(text).toContain('+250,000 KZT · +980 USD');
    expect(text).not.toMatch(/250,980/);
  });
});

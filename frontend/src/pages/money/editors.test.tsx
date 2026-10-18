// @vitest-environment jsdom
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import type { ReactElement } from 'react';
import { MemoryRouter } from 'react-router';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

import { ToastProvider } from '@/components/ui/toaster';
import type { Account, Category, Transaction } from '@/lib/ledger/types';

/**
 * The page wiring of the money editors: the PATCH body the row editor builds
 * (only what changed; a bank recalculation sends amount and note only), and
 * the POST body and the rate line of the transfer tab.
 */

const { getJson, updateMutate, transferMutate } = vi.hoisted(() => ({
  // Nothing loads over the wire: the hooks under test are mocked below.
  getJson: vi.fn(() => new Promise(() => {})),
  updateMutate: vi.fn(),
  transferMutate: vi.fn(),
}));

vi.mock('@/lib/api/client', async () => {
  const actual = await vi.importActual<typeof import('@/lib/api/client')>('@/lib/api/client');
  return { ...actual, api: { ...actual.api, getJson } };
});

const ok = <T,>(data: T) => ({
  data,
  isPending: false,
  isError: false,
  isSuccess: true,
  error: null,
  refetch: vi.fn(),
});
const mutation = (mutate: unknown) => ({ mutate, isPending: false, error: null, reset: vi.fn() });

const account = (id: string, name: string, currency: string): Account => ({
  id,
  name,
  bank: '',
  kind: 'card',
  currency,
  last4: '',
  opening_balance: 0,
  opening_date: null,
  archived: false,
  position: 0,
  balance: 0,
  last_activity: null,
  created_at: '2026-01-01T00:00:00Z',
  updated_at: '2026-01-01T00:00:00Z',
});
const accounts = [
  account('kzt', 'Kaspi', 'KZT'),
  account('usd', 'Wise', 'USD'),
  account('usd2', 'Cash', 'USD'),
];

const category = (id: string, name: string): Category => ({
  id,
  name,
  kind: 'expense',
  flexibility: 'variable',
  budget_max: null,
  budget_currency: null,
  archived: false,
  position: 0,
  created_at: '2026-01-01T00:00:00Z',
  updated_at: '2026-01-01T00:00:00Z',
});
const categories = [category('food', 'Food'), category('cafe', 'Cafe')];

const row = (over: Partial<Transaction>): Transaction => ({
  id: 't1',
  type: 'expense',
  date: '2026-10-17',
  time: null,
  account_id: 'kzt',
  currency: 'KZT',
  amount: 2500,
  category_id: 'food',
  merchant: 'Magnum',
  merchant_key: 'magnum',
  name: 'Groceries',
  receipt_amount: null,
  receipt_currency: null,
  fx_note: '',
  adjusts_id: null,
  trip: '',
  note: '',
  source: 'manual',
  status: 'posted',
  external_id: null,
  final_amount: 2500,
  adjustments: [],
  created_at: '2026-10-17T10:00:00Z',
  updated_at: '2026-10-17T10:00:00Z',
  ...over,
});

let linked: Transaction = row({});

vi.mock('@/hooks/useMoney', async () => {
  const actual = await vi.importActual<typeof import('@/hooks/useMoney')>('@/hooks/useMoney');
  const empty = () => ({
    ...ok({ pages: [{ data: [], total: 0 }], pageParams: [0] }),
    hasNextPage: false,
    isFetchingNextPage: false,
  });
  return {
    ...actual,
    useAccounts: () => ok(accounts),
    useCategories: () => ok(categories),
    useCurrencies: () => ok([]),
    useInbox: () => ok([]),
    useReport: () => ok(undefined),
    useMoneySummary: () => ok(undefined),
    useMoneySettings: () => ok({ llm_available: false }),
    useTransactions: empty,
    useTransfers: empty,
    useTransaction: () => ok(linked),
    useUpdateTransaction: () => mutation(updateMutate),
    useCreateTransfer: () => mutation(transferMutate),
  };
});

import { AddForm } from './AddForm';
import { MoneyLedgerPage } from './Ledger';

function mount(page: ReactElement, path = '/money') {
  const qc = new QueryClient({ defaultOptions: { queries: { retry: false } } });
  render(
    <QueryClientProvider client={qc}>
      <ToastProvider>
        <MemoryRouter initialEntries={[path]}>{page}</MemoryRouter>
      </ToastProvider>
    </QueryClientProvider>,
  );
}

const type = (label: RegExp | string, value: string) =>
  fireEvent.change(screen.getByLabelText(label), { target: { value } });

beforeEach(() => {
  updateMutate.mockReset();
  transferMutate.mockReset();
});
afterEach(cleanup);

describe('the ledger row editor', () => {
  it('sends only the fields that changed', () => {
    linked = row({});
    mount(<MoneyLedgerPage />, '/money?row=t1');
    type('Name', 'Groceries and water');
    type('Category', 'cafe');
    type('Note', 'with Anna');
    fireEvent.click(screen.getByRole('button', { name: 'Save' }));
    expect(updateMutate).toHaveBeenCalledTimes(1);
    expect(updateMutate.mock.calls[0][0]).toEqual({
      name: 'Groceries and water',
      category_id: 'cafe',
      note: 'with Anna',
    });
  });

  it('a bank recalculation sends the amount and the note only', () => {
    linked = row({ type: 'fx_adjustment', amount: -40, category_id: null, adjusts_id: 't0' });
    mount(<MoneyLedgerPage />, '/money?row=t1');
    // No name, date, merchant or category fields for an adjustment.
    expect(screen.queryByLabelText('Name')).toBeNull();
    expect(screen.queryByLabelText('Category')).toBeNull();
    type(/^Amount, KZT/, '-55');
    type('Note', 'bank rate');
    fireEvent.click(screen.getByRole('button', { name: 'Save' }));
    expect(updateMutate.mock.calls[0][0]).toEqual({ amount: -55, note: 'bank rate' });
  });

  it('closes without a request when nothing changed', () => {
    linked = row({});
    mount(<MoneyLedgerPage />, '/money?row=t1');
    fireEvent.click(screen.getByRole('button', { name: 'Save' }));
    expect(updateMutate).not.toHaveBeenCalled();
    expect(screen.queryByRole('button', { name: 'Save' })).toBeNull();
  });
});

describe('the transfer tab', () => {
  const openTransfer = () => {
    mount(<AddForm onClose={() => {}} />);
    fireEvent.click(screen.getByRole('tab', { name: 'Transfer' }));
  };

  it('posts a same-currency transfer without a received amount', () => {
    openTransfer();
    type('From', 'usd');
    type('To', 'usd2');
    type(/^Sent/, '150');
    type(/^Fee/, '1.5');
    type('Date', '2026-10-18');
    expect(screen.queryByLabelText(/^Received/)).toBeNull();
    fireEvent.click(screen.getByRole('button', { name: 'Add transfer' }));
    expect(transferMutate.mock.calls[0][0]).toEqual({
      date: '2026-10-18',
      from_account_id: 'usd',
      to_account_id: 'usd2',
      amount_sent: 150,
      amount_received: null,
      fee: 1.5,
      name: 'Wise → Cash',
      note: '',
    });
  });

  it('asks for the received amount across currencies and shows the rate', () => {
    openTransfer();
    type('From', 'usd');
    type('To', 'kzt');
    type(/^Sent/, '1000');
    expect(screen.queryByText(/Rate of this exchange/)).toBeNull();
    type(/^Received, KZT/, '520000');
    // 1000 USD for 520 000 KZT, turned over to read as KZT per USD.
    expect(screen.getByText(/Rate of this exchange/).textContent).toMatch(/520 KZT per USD/);
    fireEvent.click(screen.getByRole('button', { name: 'Add transfer' }));
    expect(transferMutate.mock.calls[0][0]).toMatchObject({
      amount_sent: 1000,
      amount_received: 520000,
      fee: null,
      name: 'Wise → Kaspi',
    });
  });
});

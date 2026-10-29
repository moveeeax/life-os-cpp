import {
  useInfiniteQuery,
  useQueries,
  useQuery,
  useQueryClient,
  type InfiniteData,
  type QueryClient,
} from '@tanstack/react-query';

import { useApiMutation } from '@/hooks/useApiMutation';
import { useBatchMutation } from '@/hooks/useBatchMutation';
import { useParseJob } from '@/hooks/useParseJob';
import { api } from '@/lib/api/client';
import { qk } from '@/lib/api/queryKeys';
import type { components } from '@/lib/api/schema.gen';
import { isDay, shiftDay, type AcceptLine } from '@/lib/ledger';
import type {
  AdvisorReport,
  AdvisorReportSummary,
  Account,
  AccountInput,
  AccountPatch,
  BalanceGroup,
  Category,
  CategoryInput,
  CategoryPatch,
  Currency,
  CurrencyInput,
  CurrencyPatch,
  InboxRow,
  Merchant,
  ParseJob,
  Report,
  Settings,
  SettingsInput,
  Transaction,
  TransactionInput,
  TransactionPatch,
  Transfer,
  TransferInput,
  TransferPatch,
} from '@/lib/ledger/types';

// Paths with an id go through the client's untyped overload (it does not fill
// path templates); the response types come from the same OpenAPI schemas.
const BASE = '/api/v1/money';

/**
 * A write to the setup (accounts, categories, currencies, merchants, settings)
 * invalidates the whole section and Now's digest: names and scales show up
 * everywhere.
 */
export const MONEY_WRITES = [qk.money.all(), qk.now.all()] as const;
const ALL = MONEY_WRITES;

const part = (name: string) => ['money', name] as const;

/**
 * A ledger row write (create, edit, delete, confirm, accept) touches the rows,
 * the inbox, the balances (accounts carry theirs), the reports, the summary,
 * the merchant memory and Now; not transfers, currencies or categories.
 */
export const ROW_WRITES = [
  part('transactions'),
  part('transaction'),
  part('nearby'),
  part('inbox'),
  part('balances'),
  part('accounts'),
  part('report'),
  part('summary'),
  part('merchants'),
  part('merchant-memory'),
  qk.now.all(),
] as const;

/** A transfer moves balances and the summary; the rows and the reports do not count it. */
export const TRANSFER_WRITES = [
  part('transfers'),
  part('balances'),
  part('accounts'),
  part('summary'),
  qk.now.all(),
] as const;

/**
 * Cut the paged lists under @p prefix back to their first page, so the
 * invalidation that follows a write refetches one page, not every page that
 * "Show more" loaded.
 */
export function firstPageOnly(qc: QueryClient, prefix: readonly unknown[]) {
  qc.setQueriesData<InfiniteData<unknown, unknown>>({ queryKey: prefix }, (d) =>
    d && d.pages.length > 1
      ? { pages: d.pages.slice(0, 1), pageParams: d.pageParams.slice(0, 1) }
      : d,
  );
}

/** useApiMutation that first drops the loaded pages of the paged lists it refreshes. */
function useMoneyWrite<TData, TVariables>(
  fn: (vars: TVariables) => Promise<TData>,
  invalidate: readonly (readonly unknown[])[],
  onSuccess?: (data: TData, vars: TVariables) => void,
) {
  const qc = useQueryClient();
  return useApiMutation(
    async (vars: TVariables) => {
      const out = await fn(vars);
      firstPageOnly(qc, part('transactions'));
      firstPageOnly(qc, part('transfers'));
      return out;
    },
    { invalidate, onSuccess },
  );
}

export const PARSE_POLL_MS = 2000;
/** The provider call is capped at MONEY_LLM_TIMEOUT_SECONDS; a job unfinished after this is stuck. */
export const PARSE_DEADLINE_MS = 3 * 60 * 1000;

// ── reads ──────────────────────────────────────────────────────────────────

const list = <T>(key: readonly unknown[], path: string, query?: Record<string, unknown>) =>
  ({
    queryKey: key,
    queryFn: async ({ signal }: { signal: AbortSignal }) =>
      (await api.getJson<{ data: T[] }>(`${BASE}${path}`, { query, signal })).data,
  }) as const;

export const useCurrencies = (archived = false) =>
  useQuery(
    list<Currency>(qk.money.currencies(archived), '/currencies', {
      archived: archived ? 'true' : undefined,
    }),
  );
export const useAccounts = (archived = false) =>
  useQuery(
    list<Account>(qk.money.accounts(archived), '/accounts', {
      archived: archived ? 'true' : undefined,
    }),
  );
export const useCategories = (archived = false) =>
  useQuery(
    list<Category>(qk.money.categories(archived), '/categories', {
      archived: archived ? 'true' : undefined,
    }),
  );
export const useInbox = () => useQuery(list<InboxRow>(qk.money.inbox(), '/inbox'));
export const useBalances = () =>
  useQuery(list<BalanceGroup>(qk.money.balances(), '/reports/balances'));

export interface TransactionFilter {
  from?: string;
  to?: string;
  account?: string;
  category?: string;
  currency?: string;
  type?: string;
  q?: string;
}

export const TRANSACTION_PAGE = 50;

export function useTransactions(filter: TransactionFilter) {
  const query = Object.fromEntries(
    Object.entries(filter).filter(([, v]) => v !== undefined && v !== ''),
  );
  return useInfiniteQuery({
    queryKey: qk.money.transactions(query),
    initialPageParam: 0,
    queryFn: ({ pageParam, signal }) =>
      api.getJson<{ data: Transaction[]; total: number }>(`${BASE}/transactions`, {
        query: { ...query, status: 'posted', limit: TRANSACTION_PAGE, offset: pageParam },
        signal,
      }),
    getNextPageParam: (last, pages) => {
      const loaded = pages.reduce((n, p) => n + p.data.length, 0);
      return last.data.length > 0 && loaded < last.total ? loaded : undefined;
    },
  });
}

/** One ledger row by id (a link from a task opens it). */
export function useTransaction(id: string | null) {
  return useQuery({
    queryKey: qk.money.transaction(id ?? ''),
    enabled: id !== null && id !== '',
    retry: false,
    queryFn: async ({ signal }) =>
      (await api.getJson<{ data: Transaction }>(`${BASE}/transactions/${id}`, { signal })).data,
  });
}

/** Transfers of the period, a page at a time; `total` says how many match the filter. */
export function useTransfers(from?: string, to?: string, account?: string) {
  return useInfiniteQuery({
    queryKey: qk.money.transfers(from, to, account),
    initialPageParam: 0,
    queryFn: ({ pageParam, signal }) =>
      api.getJson<{ data: Transfer[]; total: number }>(`${BASE}/transfers`, {
        query: { from, to, account, limit: TRANSACTION_PAGE, offset: pageParam },
        signal,
      }),
    getNextPageParam: (last, pages) => {
      const loaded = pages.reduce((n, p) => n + p.data.length, 0);
      return last.data.length > 0 && loaded < last.total ? loaded : undefined;
    },
  });
}

export function useMerchants(q: string) {
  return useQuery({
    queryKey: qk.money.merchants(q),
    enabled: q.trim().length >= 1,
    staleTime: 60_000,
    queryFn: async ({ signal }) =>
      (
        await api.getJson<{ data: Merchant[] }>(`${BASE}/merchants`, {
          query: { q, limit: 8 },
          signal,
        })
      ).data,
  });
}

/** Every remembered merchant (most used first), grouped by the caller. */
export function useMerchantMemory() {
  return useQuery({
    queryKey: qk.money.merchantMemory(),
    staleTime: 60_000,
    queryFn: async ({ signal }) =>
      (
        await api.getJson<{ data: Merchant[] }>(`${BASE}/merchants`, {
          query: { limit: 300 },
          signal,
        })
      ).data,
  });
}

/** Posted rows of `account` from the day before `date` to the day after. */
export function useNearbyRows(account: string, date: string) {
  return useQuery({
    queryKey: qk.money.nearby(account, date),
    enabled: account !== '' && isDay(date),
    queryFn: async ({ signal }) =>
      (
        await api.getJson<{ data: Transaction[] }>(`${BASE}/transactions`, {
          query: {
            account,
            from: shiftDay(date, -1),
            to: shiftDay(date, 1),
            status: 'posted',
            limit: 200,
          },
          signal,
        })
      ).data,
  });
}

/** A week, month or quarter around `date`, or with kind 'custom' the days `range.from`..`range.to`. */
export function useReport(
  kind: string,
  date: string,
  asIf?: string,
  range?: { from: string; to: string },
) {
  const custom = kind === 'custom' && range !== undefined;
  return useQuery({
    queryKey: qk.money.report(kind, custom ? `${range.from}..${range.to}` : date, asIf ?? ''),
    queryFn: async ({ signal }) =>
      (
        await api.getJson<{ data: Report }>(`${BASE}/reports/period`, {
          query: custom
            ? { kind, from: range.from, to: range.to, as_if: asIf || undefined }
            : { kind, date, as_if: asIf || undefined },
          signal,
        })
      ).data,
  });
}

/** The month reports of a year up to `months` (January is 1), for the ledger's month strip. */
export function useMonthReports(year: number, months: number, asIf?: string) {
  return useQueries({
    queries: Array.from({ length: months }, (_, i) => {
      const date = `${year}-${String(i + 1).padStart(2, '0')}-01`;
      return {
        queryKey: qk.money.report('month', date, asIf ?? ''),
        queryFn: async ({ signal }: { signal: AbortSignal }) =>
          (
            await api.getJson<{ data: Report }>(`${BASE}/reports/period`, {
              query: { kind: 'month', date, as_if: asIf || undefined },
              signal,
            })
          ).data,
        staleTime: 5 * 60_000,
      };
    }),
  });
}

export function useMoneySettings() {
  return useQuery({
    queryKey: qk.money.settings(),
    queryFn: async ({ signal }) =>
      (
        await api.getJson<{ data: Settings & { llm_available?: boolean } }>(`${BASE}/settings`, {
          signal,
        })
      ).data,
  });
}

// ── writes ─────────────────────────────────────────────────────────────────

export function useCreateTransaction(onSuccess?: (row: Transaction) => void) {
  return useMoneyWrite(
    async (body: TransactionInput) =>
      (await api.postJson<{ data: Transaction }>(`${BASE}/transactions`, { body })).data,
    ROW_WRITES,
    onSuccess,
  );
}

export function useUpdateTransaction(id: string, onSuccess?: () => void) {
  return useMoneyWrite(
    async (body: TransactionPatch) =>
      (await api.patchJson<{ data: Transaction }>(`${BASE}/transactions/${id}`, { body })).data,
    ROW_WRITES,
    onSuccess,
  );
}

export function useDeleteTransaction(onSuccess?: () => void) {
  return useMoneyWrite(
    (id: string) => api.deleteJson<unknown>(`${BASE}/transactions/${id}`),
    ROW_WRITES,
    onSuccess,
  );
}

/** Posts inbox rows one after another; one call, one error, one refetch. */
export function useConfirmTransactions() {
  const qc = useQueryClient();
  return useBatchMutation(
    async (id: string) => {
      const out = await api.postJson<{ data: Transaction }>(`${BASE}/transactions/${id}/confirm`);
      firstPageOnly(qc, part('transactions'));
      return out;
    },
    { invalidate: ROW_WRITES },
  );
}

export function useCreateTransfer(onSuccess?: () => void) {
  return useMoneyWrite(
    async (body: TransferInput) =>
      (await api.postJson<{ data: Transfer }>(`${BASE}/transfers`, { body })).data,
    TRANSFER_WRITES,
    onSuccess,
  );
}

export function useUpdateTransfer(id: string, onSuccess?: () => void) {
  return useMoneyWrite(
    async (body: TransferPatch) =>
      (await api.patchJson<{ data: Transfer }>(`${BASE}/transfers/${id}`, { body })).data,
    TRANSFER_WRITES,
    onSuccess,
  );
}

export function useDeleteTransfer(onSuccess?: () => void) {
  return useMoneyWrite(
    (id: string) => api.deleteJson<unknown>(`${BASE}/transfers/${id}`),
    TRANSFER_WRITES,
    onSuccess,
  );
}

/** A new code goes through POST (upsert); an existing one through PATCH. */
export function useSaveCurrency(code: string | null, onSuccess?: () => void) {
  return useApiMutation(
    async (body: CurrencyInput | CurrencyPatch) =>
      code
        ? (await api.patchJson<{ data: Currency }>(`${BASE}/currencies/${code}`, { body })).data
        : (await api.postJson<{ data: Currency }>(`${BASE}/currencies`, { body })).data,
    { invalidate: ALL, onSuccess },
  );
}

/** Where a merchant's next rows are suggested; past rows keep their category. */
export function useMoveMerchant(onSuccess?: () => void) {
  return useApiMutation(
    async (body: { merchant_key: string; category_id: string | null }) =>
      (await api.patchJson<{ data: Merchant }>(`${BASE}/merchants`, { body })).data,
    { invalidate: ALL, onSuccess },
  );
}

export function useSaveAccount(id: string | null, onSuccess?: () => void) {
  return useApiMutation(
    async (body: AccountInput | AccountPatch) =>
      id
        ? (await api.patchJson<{ data: Account }>(`${BASE}/accounts/${id}`, { body })).data
        : (await api.postJson<{ data: Account }>(`${BASE}/accounts`, { body })).data,
    { invalidate: ALL, onSuccess },
  );
}

export function useDeleteAccount(onSuccess?: (outcome: string) => void) {
  return useApiMutation(
    async (id: string) =>
      (await api.deleteJson<{ outcome: string }>(`${BASE}/accounts/${id}`)).outcome,
    { invalidate: ALL, onSuccess },
  );
}

export function useSaveCategory(id: string | null, onSuccess?: () => void) {
  return useApiMutation(
    async (body: CategoryInput | CategoryPatch) =>
      id
        ? (await api.patchJson<{ data: Category }>(`${BASE}/categories/${id}`, { body })).data
        : (await api.postJson<{ data: Category }>(`${BASE}/categories`, { body })).data,
    { invalidate: ALL, onSuccess },
  );
}

export function useDeleteCategory(onSuccess?: (outcome: string) => void) {
  return useApiMutation(
    async (id: string) =>
      (await api.deleteJson<{ outcome: string }>(`${BASE}/categories/${id}`)).outcome,
    { invalidate: ALL, onSuccess },
  );
}

export function useSaveSettings(onSuccess?: () => void) {
  return useApiMutation(
    async (body: SettingsInput) =>
      (await api.putJson<{ data: Settings }>(`${BASE}/settings`, { body })).data,
    { invalidate: ALL, onSuccess },
  );
}

// ── the parse ──────────────────────────────────────────────────────────────

export type { ParseState } from '@/hooks/useParseJob';

export type ParseStart =
  | { kind: 'text'; text: string; hint_date?: string }
  | { kind: 'receipt'; image: string; hint_date?: string };

/**
 * One parse of the add form: start, poll every 2 s until done or failed (or
 * the deadline), accept the edited lines into the inbox. cancel() and unmount
 * drop the job: a reopened form starts clean.
 */
export function useMoneyParse() {
  return useParseJob({
    enqueue: async (body: ParseStart) => {
      const path = body.kind === 'receipt' ? `${BASE}/parse/receipt` : `${BASE}/parse`;
      const payload =
        body.kind === 'receipt'
          ? { image: body.image, hint_date: body.hint_date }
          : { text: body.text, hint_date: body.hint_date };
      const { data } = await api.postJson<{ data: { id: string } }>(path, { body: payload });
      return { id: data.id, status: 'queued' } as ParseJob;
    },
    read: async (id, signal) =>
      (await api.getJson<{ data: ParseJob }>(`${BASE}/parse/${id}`, { signal })).data,
    pollMs: PARSE_POLL_MS,
    deadlineMs: PARSE_DEADLINE_MS,
    notConfigured: 'not_configured',
  });
}

export function useAcceptParse(jobId: string | undefined, onSuccess?: () => void) {
  return useMoneyWrite(
    async (lines: AcceptLine[]) =>
      (
        await api.postJson<{ data: Transaction[] }>(`${BASE}/parse/${jobId}/accept`, {
          body: { lines },
        })
      ).data,
    ROW_WRITES,
    onSuccess,
  );
}

// ── the advisor ────────────────────────────────────────────────────────────

/** The reviews; refreshes while one is queued or running. */
export function useAdvisorReports() {
  return useQuery({
    queryKey: qk.money.advisorReports(),
    queryFn: async ({ signal }) =>
      (await api.getJson<{ data: AdvisorReportSummary[] }>(`${BASE}/advisor/reports`, { signal }))
        .data,
    refetchInterval: (q) =>
      q.state.data?.some((r) => r.status === 'queued' || r.status === 'running') ? 3000 : false,
  });
}

export function useAdvisorReport(id: string | undefined, status?: string) {
  return useQuery({
    queryKey: qk.money.advisorReport(id ?? '', status ?? ''),
    enabled: !!id,
    queryFn: async ({ signal }) =>
      (await api.getJson<{ data: AdvisorReport }>(`${BASE}/advisor/reports/${id}`, { signal }))
        .data,
    // A finished review does not change; the status in the key reads it again when it turns done.
    staleTime: Infinity,
  });
}

export function useRunAdvisor(onSuccess?: () => void) {
  return useApiMutation(
    async (body: { period: 'week' | 'month' | 'quarter'; date?: string }) =>
      (
        await api.postJson<{ data: AdvisorReport; queued: boolean }>(`${BASE}/advisor/run`, {
          body,
        })
      ).data,
    { invalidate: [qk.money.advisorReports()], onSuccess },
  );
}

/** All accounts in one currency at the day's rates, with the days of the period. */
export function useMoneySummary(currency: string, days: number, enabled = true) {
  return useQuery({
    enabled,
    queryKey: qk.money.summary(currency, days),
    queryFn: async ({ signal }) =>
      (
        await api.getJson<{ data: components['schemas']['MoneySummary'] }>(`${BASE}/summary`, {
          query: { currency, days },
          signal,
        })
      ).data,
  });
}

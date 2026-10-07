import { useCallback, useEffect, useRef, useState } from 'react';
import { useInfiniteQuery, useQuery } from '@tanstack/react-query';

import { useApiMutation } from '@/hooks/useApiMutation';
import { ApiClientError, api, apiErrorMessage } from '@/lib/api/client';
import { qk } from '@/lib/api/queryKeys';
import type { AcceptLine } from '@/lib/ledger';
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

/** Every write invalidates the whole section: balances, totals and reports all move. */
const ALL = [qk.money.all()] as const;

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

export const useCurrencies = () => useQuery(list<Currency>(qk.money.currencies(), '/currencies'));
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

export function useTransfers(from?: string, to?: string, account?: string) {
  return useQuery({
    queryKey: qk.money.transfers(from, to, account),
    queryFn: async ({ signal }) =>
      (
        await api.getJson<{ data: Transfer[] }>(`${BASE}/transfers`, {
          query: { from, to, account, limit: 200 },
          signal,
        })
      ).data,
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

export function useReport(kind: string, date: string, asIf?: string) {
  return useQuery({
    queryKey: qk.money.report(kind, date, asIf ?? ''),
    queryFn: async ({ signal }) =>
      (
        await api.getJson<{ data: Report }>(`${BASE}/reports/period`, {
          query: { kind, date, as_if: asIf || undefined },
          signal,
        })
      ).data,
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

/** Whether the money module answers at all (404 while switched off). */
export function useMoneyEnabled(signedIn: boolean): boolean {
  const probe = useQuery({
    queryKey: qk.money.enabled(),
    enabled: signedIn,
    staleTime: Infinity,
    retry: false,
    queryFn: async ({ signal }) => {
      const { error } = await api.GET(`${BASE}/settings` as string, { signal });
      return !(error && error.status === 404);
    },
  });
  return probe.data ?? true;
}

// ── writes ─────────────────────────────────────────────────────────────────

export function useCreateTransaction(onSuccess?: (row: Transaction) => void) {
  return useApiMutation(
    async (body: TransactionInput) =>
      (await api.postJson<{ data: Transaction }>(`${BASE}/transactions`, { body })).data,
    { invalidate: ALL, onSuccess },
  );
}

export function useUpdateTransaction(id: string, onSuccess?: () => void) {
  return useApiMutation(
    async (body: TransactionPatch) =>
      (await api.patchJson<{ data: Transaction }>(`${BASE}/transactions/${id}`, { body })).data,
    { invalidate: ALL, onSuccess },
  );
}

export function useDeleteTransaction(onSuccess?: () => void) {
  return useApiMutation((id: string) => api.deleteJson<unknown>(`${BASE}/transactions/${id}`), {
    invalidate: ALL,
    onSuccess,
  });
}

export function useConfirmTransaction() {
  return useApiMutation(
    async (id: string) =>
      (await api.postJson<{ data: Transaction }>(`${BASE}/transactions/${id}/confirm`)).data,
    { invalidate: ALL },
  );
}

export function useCreateTransfer(onSuccess?: () => void) {
  return useApiMutation(
    async (body: TransferInput) =>
      (await api.postJson<{ data: Transfer }>(`${BASE}/transfers`, { body })).data,
    { invalidate: ALL, onSuccess },
  );
}

export function useUpdateTransfer(id: string, onSuccess?: () => void) {
  return useApiMutation(
    async (body: TransferPatch) =>
      (await api.patchJson<{ data: Transfer }>(`${BASE}/transfers/${id}`, { body })).data,
    { invalidate: ALL, onSuccess },
  );
}

export function useDeleteTransfer(onSuccess?: () => void) {
  return useApiMutation((id: string) => api.deleteJson<unknown>(`${BASE}/transfers/${id}`), {
    invalidate: ALL,
    onSuccess,
  });
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

export type ParseState = 'idle' | 'starting' | 'running' | 'done' | 'failed';

export type ParseStart =
  | { kind: 'text'; text: string; hint_date?: string }
  | { kind: 'receipt'; image: string; hint_date?: string };

/**
 * One parse of the add form: start, poll every 2 s until done or failed (or
 * the deadline), accept the edited lines into the inbox. cancel() and unmount
 * drop the job: a reopened form starts clean.
 */
export function useMoneyParse() {
  const [state, setState] = useState<ParseState>('idle');
  const [job, setJob] = useState<ParseJob | null>(null);
  const [error, setError] = useState<string | null>(null);
  const starting = useRef(false);
  const generation = useRef(0);

  const start = useCallback(async (body: ParseStart) => {
    if (starting.current) return;
    starting.current = true;
    const mine = ++generation.current;
    setState('starting');
    setError(null);
    setJob(null);
    try {
      const path = body.kind === 'receipt' ? `${BASE}/parse/receipt` : `${BASE}/parse`;
      const payload =
        body.kind === 'receipt'
          ? { image: body.image, hint_date: body.hint_date }
          : { text: body.text, hint_date: body.hint_date };
      const { data } = await api.postJson<{ data: { id: string } }>(path, { body: payload });
      if (generation.current !== mine) return;
      setJob({ id: data.id, status: 'queued' } as ParseJob);
      setState('running');
    } catch (e) {
      if (generation.current !== mine) return;
      setError(
        e instanceof ApiClientError && e.code === 'not_configured'
          ? 'not_configured'
          : apiErrorMessage(e),
      );
      setState('failed');
    } finally {
      starting.current = false;
    }
  }, []);

  const cancel = useCallback(() => {
    generation.current++;
    starting.current = false;
    setState('idle');
    setJob(null);
    setError(null);
  }, []);

  const jobId = state === 'running' ? job?.id : undefined;

  useEffect(() => {
    if (!jobId) return;
    const mine = generation.current;
    const deadline = Date.now() + PARSE_DEADLINE_MS;
    let timer: ReturnType<typeof setTimeout> | undefined;
    const controller = new AbortController();
    const tick = async () => {
      if (generation.current !== mine) return;
      if (Date.now() > deadline) {
        setError('parse_timeout');
        setState('failed');
        return;
      }
      try {
        const { data } = await api.getJson<{ data: ParseJob }>(`${BASE}/parse/${jobId}`, {
          signal: controller.signal,
        });
        if (generation.current !== mine) return;
        setJob(data);
        if (data.status === 'done') {
          setState('done');
          return;
        }
        if (data.status === 'failed') {
          setError(data.error);
          setState('failed');
          return;
        }
      } catch (e) {
        if (generation.current !== mine || controller.signal.aborted) return;
        if (e instanceof ApiClientError && e.status === 404) {
          setError('The parse job is gone.');
          setState('failed');
          return;
        }
      }
      timer = setTimeout(() => void tick(), PARSE_POLL_MS);
    };
    void tick();
    return () => {
      controller.abort();
      clearTimeout(timer);
    };
  }, [jobId]);

  useEffect(() => () => void generation.current++, []);

  return { state, job, error, start, cancel };
}

export function useAcceptParse(jobId: string | undefined, onSuccess?: () => void) {
  return useApiMutation(
    async (lines: AcceptLine[]) =>
      (
        await api.postJson<{ data: Transaction[] }>(`${BASE}/parse/${jobId}/accept`, {
          body: { lines },
        })
      ).data,
    { invalidate: ALL, onSuccess },
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

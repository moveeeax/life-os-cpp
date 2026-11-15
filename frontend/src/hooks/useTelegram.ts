import { useQuery, type UseQueryOptions } from '@tanstack/react-query';

import { api } from '@/lib/api/client';
import { qk } from '@/lib/api/queryKeys';

/** A private chat waiting for the user's answer (GET /api/v1/telegram/waiting). */
export interface TelegramWaiting {
  chat: string;
  title: string;
  since: string;
}

const WAITING = '/api/v1/telegram/waiting';

/** The chats waiting for an answer; a 404 means Telegram is not linked. */
export async function fetchTelegramWaiting(signal?: AbortSignal): Promise<TelegramWaiting[]> {
  return (await api.getJson<{ data: TelegramWaiting[] }>(WAITING, signal ? { signal } : undefined))
    .data;
}

/** What every reader shares: one key, ten minutes fresh, no retry (unlinked answers 404). */
export const TELEGRAM_WAITING_STALE_MS = 10 * 60_000;

type WaitingOptions = Omit<UseQueryOptions<TelegramWaiting[]>, 'queryKey' | 'queryFn'>;

/** The chats waiting for an answer: Now's widget, the evening mode card and Ani's lines. */
export function useTelegramWaiting(options: WaitingOptions = {}) {
  return useQuery({
    queryKey: qk.telegramWaiting(),
    staleTime: TELEGRAM_WAITING_STALE_MS,
    retry: false,
    queryFn: ({ signal }) => fetchTelegramWaiting(signal),
    ...options,
  });
}

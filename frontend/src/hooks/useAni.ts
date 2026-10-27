import { useQuery } from '@tanstack/react-query';

import { useApiMutation } from '@/hooks/useApiMutation';
import { api } from '@/lib/api/client';
import { qk } from '@/lib/api/queryKeys';
import type { components } from '@/lib/api/schema.gen';

export type AniPrefs = components['schemas']['AniPrefs'];
export type AniBare = components['schemas']['AniBare'];
export type AniWebhook = components['schemas']['AniWebhook'];

const PREFS = '/api/v1/ani/prefs';
const WEBHOOK = '/api/v1/ani/webhook';

/** Ani's settings kept on the server (the bare outfit switch). */
export function useAniPrefs(enabled = true) {
  return useQuery({
    queryKey: qk.ani.prefs(),
    enabled,
    staleTime: 60_000,
    queryFn: async ({ signal }) => (await api.getJson<{ data: AniPrefs }>(PREFS, { signal })).data,
  });
}

/** Turns bare on (with the user's 18+ word) or off. */
export function useSaveAniPrefs() {
  return useApiMutation(
    async (bare: boolean) =>
      (
        await api.putJson<{ data: AniPrefs }>(PREFS, {
          body: bare ? { bare: true, adult: true } : { bare: false },
        })
      ).data,
    { invalidate: [qk.ani.prefs()] },
  );
}

/** Two short signed links to the bare model; a 403 when bare is off. */
export async function fetchAniBare(): Promise<AniBare> {
  return (await api.getJson<{ data: AniBare }>('/api/v1/ani/bare')).data;
}

/** The user's webhook URL (made on first read) and its last calls. */
export function useAniWebhook() {
  return useQuery({
    queryKey: qk.ani.webhook(),
    queryFn: async ({ signal }) =>
      (await api.getJson<{ data: AniWebhook }>(WEBHOOK, { signal })).data,
  });
}

export function useRotateAniWebhook() {
  return useApiMutation(
    async () => (await api.postJson<{ data: AniWebhook }>(`${WEBHOOK}/rotate`)).data,
    { invalidate: [qk.ani.webhook()] },
  );
}

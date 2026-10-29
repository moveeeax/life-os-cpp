import { useQuery, useQueryClient } from '@tanstack/react-query';
import { useCallback } from 'react';

import { useApiMutation } from '@/hooks/useApiMutation';
import { api } from '@/lib/api/client';
import { qk } from '@/lib/api/queryKeys';
import type { components } from '@/lib/api/schema.gen';

export type AniPrefs = components['schemas']['AniPrefs'];
export type AniBare = components['schemas']['AniBare'];
export type AniWebhook = components['schemas']['AniWebhook'];
export type ApiKey = components['schemas']['ApiKey'];
export type ApiKeyCreated = components['schemas']['ApiKeyCreated'];

const PREFS = '/api/v1/ani/prefs';
const WEBHOOK = '/api/v1/ani/webhook';
const KEYS = '/api/v1/account/api-keys';

/** Ani's settings kept on the server (the bare outfit switch). */
export function useAniPrefs(enabled = true) {
  return useQuery({
    queryKey: qk.ani.prefs(),
    enabled,
    staleTime: 60_000,
    queryFn: async ({ signal }) => (await api.getJson<{ data: AniPrefs }>(PREFS, { signal })).data,
  });
}

/**
 * "Not about this": mutes a kind of Ani's lines. The prefs cache is
 * updated first, so a second mute before the refetch keeps the first.
 */
export function useMuteKind() {
  const qc = useQueryClient();
  return useCallback(
    (kind: NonNullable<AniPrefs['muted']>[number]) => {
      const prefs = qc.getQueryData<AniPrefs>(qk.ani.prefs());
      const muted = [...new Set([...(prefs?.muted ?? []), kind])] as AniPrefs['muted'];
      if (prefs) qc.setQueryData<AniPrefs>(qk.ani.prefs(), { ...prefs, muted });
      void api
        .putJson(PREFS, { body: { muted } })
        .then(() => qc.invalidateQueries({ queryKey: qk.ani.prefs() }))
        .catch(() => {});
    },
    [qc],
  );
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

/** The user's API keys that work only for Ani's MCP (scope ani). */
export function useAniMcpKeys() {
  return useQuery({
    queryKey: qk.ani.mcpKeys(),
    queryFn: async ({ signal }) =>
      (await api.getJson<{ data: ApiKey[] }>(KEYS, { signal })).data.filter(
        (k) => k.scope === 'ani' && !k.revoked_at,
      ),
  });
}

/** Makes a key for Ani's MCP; the plaintext key comes back only here. */
export function useCreateAniMcpKey() {
  return useApiMutation(
    async () =>
      await api.postJson<ApiKeyCreated>(KEYS, { body: { name: 'Ani MCP', scope: 'ani' } }),
    { invalidate: [qk.ani.mcpKeys()] },
  );
}

export function useRevokeAniMcpKey() {
  return useApiMutation(async (id: string) => await api.deleteJson(`${KEYS}/${id}`), {
    invalidate: [qk.ani.mcpKeys()],
  });
}

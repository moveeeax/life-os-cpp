import { useQuery } from '@tanstack/react-query';

import { useApiMutation } from '@/hooks/useApiMutation';
import { api } from '@/lib/api/client';
import { qk } from '@/lib/api/queryKeys';
import type { components } from '@/lib/api/schema.gen';

export type ApiKey = components['schemas']['ApiKey'];
export type ApiKeyCreated = components['schemas']['ApiKeyCreated'];
export type ApiKeyScope = 'full' | 'read';

const KEYS = '/api/v1/account/api-keys';

/** The caller's live keys for scripts and agents; Ani's MCP keys live in her card. */
export function useApiKeys() {
  return useQuery({
    queryKey: qk.apiKeys(),
    queryFn: async ({ signal }) =>
      (await api.getJson<{ data: ApiKey[] }>(KEYS, { signal })).data.filter(
        (k) => k.scope !== 'ani' && !k.revoked_at,
      ),
  });
}

/** Makes a key; the plaintext key comes back only here. */
export function useCreateApiKey() {
  return useApiMutation(
    async (input: { name: string; scope: ApiKeyScope }) =>
      await api.postJson<ApiKeyCreated>(KEYS, { body: input }),
    { invalidate: [qk.apiKeys()] },
  );
}

export function useRevokeApiKey() {
  return useApiMutation(async (id: string) => await api.deleteJson(`${KEYS}/${id}`), {
    invalidate: [qk.apiKeys()],
  });
}

import { useApiMutation } from '@/hooks/useApiMutation';
import { api } from '@/lib/api/client';

/**
 * The CRUD plumbing the section hooks share. A collection lives at one URL:
 * GET lists it as `{ data: T[] }`, POST adds a row, PATCH `url/{id}` edits one
 * and DELETE `url/{id}` drops one. Each section keeps its own exported hooks
 * and invalidation lists; only the bodies come from here.
 */

type Keys = readonly (readonly unknown[])[];

/** Query options that list a collection and unwrap its `data`. */
export function listQuery<T>(
  key: readonly unknown[],
  url: string,
  query?: Record<string, string | number>,
) {
  return {
    queryKey: key,
    queryFn: async ({ signal }: { signal: AbortSignal }) =>
      (await api.getJson<{ data: T[] }>(url, query ? { query, signal } : { signal })).data,
  };
}

/** POST to `url` when `id` is null, else PATCH `url/{id}`; resolves to the saved row. */
export function useSaveEntity<I, T>(
  url: string,
  id: string | null,
  options: { invalidate: Keys; onSuccess?: (row: T) => void },
) {
  return useApiMutation(
    async (body: I) =>
      id
        ? (await api.patchJson<{ data: T }>(`${url}/${id}`, { body })).data
        : (await api.postJson<{ data: T }>(url, { body })).data,
    { invalidate: options.invalidate, onSuccess: options.onSuccess },
  );
}

/** DELETE `url/{id}`; the mutation variable is the id. */
export function useDeleteEntity(
  url: string,
  options: { invalidate: Keys; onSuccess?: () => void },
) {
  return useApiMutation((id: string) => api.deleteJson<unknown>(`${url}/${id}`), {
    invalidate: options.invalidate,
    onSuccess: options.onSuccess,
  });
}

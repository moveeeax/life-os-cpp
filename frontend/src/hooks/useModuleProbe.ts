import { useQuery } from '@tanstack/react-query';

import { api } from '@/lib/api/client';

/** The modules GET /api/v1/modules answers for. */
export type ModuleId =
  | 'billing'
  | 'fitness'
  | 'food'
  | 'goals'
  | 'habits'
  | 'money'
  | 'notes'
  | 'now'
  | 'practice'
  | 'tasks'
  | 'travel'
  | 'workout';

export const MODULES_KEY = ['modules'] as const;

/**
 * Whether a module answers at all: a cheap GET that 404s while the module is
 * switched off on the server. Any other answer, errors included, counts as on.
 * Undefined while the probe runs or while it is not enabled.
 */
export function useModuleProbe(
  queryKey: readonly unknown[],
  url: string,
  enabled: boolean,
): boolean | undefined {
  const probe = useQuery({
    queryKey,
    enabled,
    staleTime: Infinity,
    retry: false,
    queryFn: async ({ signal }) => {
      const { error } = await api.GET(url, { signal });
      return !(error && error.status === 404);
    },
  });
  return probe.data;
}

/** The module switches in one call; every caller shares the one query. */
export function useModules(enabled: boolean) {
  return useQuery({
    queryKey: MODULES_KEY,
    enabled,
    staleTime: Infinity,
    retry: false,
    queryFn: async ({ signal }) =>
      (
        await api.getJson<{ data: Partial<Record<ModuleId, boolean>> }>('/api/v1/modules', {
          signal,
        })
      ).data,
  });
}

/**
 * Whether module @p id is on, from GET /api/v1/modules. When that call fails
 * (an older server, a network error) or leaves the module out, the module's own
 * probe decides: a 404 there means off. Undefined while unknown or not enabled.
 */
export function useModuleOn(
  id: ModuleId,
  probe: readonly [readonly unknown[], string],
  enabled: boolean,
): boolean | undefined {
  const modules = useModules(enabled);
  const flag = modules.data?.[id];
  const fallback = enabled && (modules.isError || (modules.isSuccess && typeof flag !== 'boolean'));
  const probed = useModuleProbe(probe[0], probe[1], fallback);
  if (!enabled) return undefined;
  if (typeof flag === 'boolean') return flag;
  return fallback ? probed : undefined;
}

/** A probe has settled when it answered, or when it is not going to run. */
export function probesSettled(probes: readonly { enabled: boolean; on: unknown }[]): boolean {
  return probes.every((p) => !p.enabled || p.on !== undefined);
}

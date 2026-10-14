import { useQuery } from '@tanstack/react-query';

import { api } from '@/lib/api/client';

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

/** A probe has settled when it answered, or when it is not going to run. */
export function probesSettled(probes: readonly { enabled: boolean; on: unknown }[]): boolean {
  return probes.every((p) => !p.enabled || p.on !== undefined);
}
